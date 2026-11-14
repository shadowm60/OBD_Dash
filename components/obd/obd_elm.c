/*
 * obd_elm - obd_client backend for an ELM327 adapter on a serial link
 * (BLE adapter via ble_svc, wired through obd_client_set_elm_io()).
 *
 * The adapter does ISO-TP itself (CAF1, flow control automatic). With
 * headers on (ATH1) and spaces off (ATS0) every received CAN frame is one
 * line, "7E8064100BE3FA813" = ID, PCI, data; frames are reassembled here so
 * the responder ID is known and multi-frame answers come out whole.
 *
 * Set-up (first request, again after the console or a reconnect touched the
 * adapter): ATZ ATE0 ATL0 ATS0 ATH1 ATSP6 (CAN 11-bit 500 kbit/s, no protocol
 * search). Per request only what changed: ATSH <tx id>, ATCRA <rx id> / ATCRA
 * (functional: any 7E8..7EF). A command runs until the '>' prompt.
 *
 * Speed: when the caller knows how many CAN frames the answer takes, that
 * count is appended as one hex digit ("010C0D1"): the adapter returns as soon
 * as they are in instead of waiting its timeout for more ECUs. If the adapter
 * rejects the digit ('?') it is not used again; if a multi-frame answer comes
 * back cut short, the request is sent again at once without the count and
 * counts are only used for single frame answers from then on (the caller
 * never sees the cut answer). obd_elm_single_frame_fast(): callers can keep
 * answers to one frame to stay on the fast path.
 * The average request time is logged every 100 requests.
 */
#include "obd_elm.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "obd_elm";

#define LINE_MAX_       96
#define CMD_TIMEOUT_MS  1500
#define ATZ_TIMEOUT_MS  4000
#define REQ_TIMEOUT_MS  2500    /* BLE round trip + ELM timeout (ATST) + reply */

static obd_elm_io_t s_io;
static bool     s_inited;
static uint32_t s_cur_tx, s_cur_rx = UINT32_MAX;   /* what the adapter is set to */
static bool     s_mon;                      /* ATMA running (monitor, see the end) */
static bool     s_count_ok = true;          /* adapter accepts the response count digit */
static bool     s_count_mf_ok = true;       /* ... also for multi-frame answers */
static uint8_t  s_req_frames;               /* count sent with this request, 0 = none */
static uint32_t s_req_start;
static uint32_t s_stat_n, s_stat_sum_ms, s_stat_max_ms;
static uint8_t  s_last_req[7];              /* for the resend without count */
static uint16_t s_last_len;

/* request in progress */
static bool     s_pending;
static uint32_t s_deadline;
static char     s_line[LINE_MAX_];
static int      s_line_len;
static bool     s_saw_data, s_saw_nodata, s_saw_error;
static char     s_error[24];

/* ISO-TP reassembly of one responder */
static uint32_t s_asm_id;
static uint8_t  s_asm[OBD_ELM_MAX_MSG];
static uint16_t s_asm_len, s_asm_total;
static uint8_t  s_asm_sn;

static esp_err_t send_payload(const uint8_t *req, uint16_t len, uint8_t frames);
void obd_elm_monitor_stop(void);

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void obd_elm_set_io(const obd_elm_io_t *io)
{
    s_io = *io;
    s_inited = false;
}

void obd_elm_invalidate(void)
{
    s_inited = false;
}

bool obd_elm_ready(void)
{
    return s_io.ready && s_io.ready();
}

const char *obd_elm_error(void)
{
    return s_error;
}

/* ---------------------------------------------------------------- blocking commands (set-up) */

static void drain(void)
{
    char b[64];
    while (s_io.read(b, sizeof(b), 0) > 0) {
    }
}

/* Sends `cmd`, collects the reply up to the '>' prompt into out (lines joined
 * with '|'). Returns false on timeout or if the link is gone. */
static bool cmd(const char *c, char *out, size_t len, uint32_t timeout_ms)
{
    char tx[40];
    int n = snprintf(tx, sizeof(tx), "%s\r", c);
    drain();
    if (s_io.write(tx, (size_t)n) != ESP_OK) {
        return false;
    }
    size_t o = 0;
    out[0] = '\0';
    uint32_t end = now_ms() + timeout_ms;
    while ((int32_t)(now_ms() - end) < 0) {
        char b[64];
        size_t r = s_io.read(b, sizeof(b), 20);
        for (size_t i = 0; i < r; i++) {
            char ch = b[i];
            if (ch == '>') {
                while (o && out[o - 1] == '|') {
                    out[--o] = '\0';
                }
                return true;
            }
            if (ch == '\r' || ch == '\n') {
                ch = '|';
                if (o == 0 || out[o - 1] == '|') {
                    continue;
                }
            }
            if (o + 1 < len && (isprint((unsigned char)ch))) {
                out[o++] = ch;
                out[o] = '\0';
            }
        }
    }
    ESP_LOGW(TAG, "%s: no prompt (got '%s')", c, out);
    return false;
}

static bool cmd_ok(const char *c)
{
    char r[48];
    if (!cmd(c, r, sizeof(r), CMD_TIMEOUT_MS)) {
        return false;
    }
    /* "OK", or the echo of the command then "OK" if echo was still on */
    if (strstr(r, "OK") == NULL) {
        ESP_LOGW(TAG, "%s -> '%s'", c, r);
        return false;
    }
    return true;
}

static bool init_adapter(void)
{
    char r[64];
    snprintf(s_error, sizeof(s_error), "adapter set-up failed");
    if (!cmd("ATZ", r, sizeof(r), ATZ_TIMEOUT_MS)) {
        return false;
    }
    ESP_LOGI(TAG, "adapter: %s", r);
    static const char *const SETUP[] = { "ATE0", "ATL0", "ATS0", "ATH1", "ATSP6", "ATAT1" };
    for (size_t i = 0; i < sizeof(SETUP) / sizeof(SETUP[0]); i++) {
        if (!cmd_ok(SETUP[i])) {
            return false;
        }
    }
    s_cur_tx = 0;
    s_cur_rx = UINT32_MAX;
    s_inited = true;
    s_error[0] = '\0';
    ESP_LOGI(TAG, "adapter ready (CAN 11-bit 500 kbit/s)");
    return true;
}

/* ---------------------------------------------------------------- request */

esp_err_t obd_elm_request(uint32_t tx_id, uint32_t rx_id, const uint8_t *req, uint16_t len, uint8_t frames)
{
    if (!obd_elm_ready()) {
        snprintf(s_error, sizeof(s_error), "adapter not connected");
        return ESP_ERR_INVALID_STATE;
    }
    if (len == 0 || len > 7) {
        return ESP_ERR_INVALID_ARG;     /* single frame requests only (ELM327 limit) */
    }
    if (s_mon) {
        obd_elm_monitor_stop();         /* requests and ATMA don't mix */
    }
    if (!s_inited && !init_adapter()) {
        return ESP_FAIL;
    }
    char c[24];
    if (tx_id != s_cur_tx) {
        snprintf(c, sizeof(c), "ATSH%03lX", (unsigned long)tx_id);
        if (!cmd_ok(c)) {
            s_inited = false;
            return ESP_FAIL;
        }
        s_cur_tx = tx_id;
    }
    if (rx_id != s_cur_rx) {
        if (rx_id) {
            snprintf(c, sizeof(c), "ATCRA%03lX", (unsigned long)rx_id);
        } else {
            snprintf(c, sizeof(c), "ATCRA");        /* back to automatic */
        }
        if (!cmd_ok(c)) {
            s_inited = false;
            return ESP_FAIL;
        }
        s_cur_rx = rx_id;
    }

    memcpy(s_last_req, req, len);
    s_last_len = len;
    return send_payload(req, len, frames);
}

bool obd_elm_single_frame_fast(void)
{
    return s_count_ok;
}

static esp_err_t send_payload(const uint8_t *req, uint16_t len, uint8_t frames)
{
    char hex[2 * 7 + 3];
    for (uint16_t i = 0; i < len; i++) {
        snprintf(hex + 2 * i, 3, "%02X", req[i]);
    }
    if (frames > 15 || !s_count_ok || (frames > 1 && !s_count_mf_ok)) {
        frames = 0;
    }
    s_req_frames = frames;
    if (frames) {
        char d[2] = { "0123456789ABCDEF"[frames], '\0' };
        strcat(hex, d);
    }
    strcat(hex, "\r");
    drain();
    s_line_len = 0;
    s_saw_data = s_saw_nodata = s_saw_error = false;
    s_error[0] = '\0';
    s_asm_id = 0;
    s_asm_len = s_asm_total = 0;
    if (s_io.write(hex, strlen(hex)) != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "BLE write failed");
        return ESP_FAIL;
    }
    s_pending = true;
    s_req_start = now_ms();
    s_deadline = s_req_start + REQ_TIMEOUT_MS;
    return ESP_OK;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* One CAN frame line: "7E8" + bytes. Reassembles ISO-TP, calls cb with whole messages. */
static void on_frame_line(const char *l, obd_elm_msg_cb_t cb, void *arg)
{
    size_t n = strlen(l);
    if (n < 5 || (n - 3) % 2 != 0) {
        return;
    }
    uint32_t id = 0;
    for (int i = 0; i < 3; i++) {
        int v = hexval(l[i]);
        if (v < 0) return;
        id = id << 4 | (uint32_t)v;
    }
    uint8_t b[8];
    int nb = 0;
    for (size_t i = 3; i + 1 < n && nb < 8; i += 2) {
        int hi = hexval(l[i]), lo = hexval(l[i + 1]);
        if (hi < 0 || lo < 0) return;
        b[nb++] = (uint8_t)(hi << 4 | lo);
    }
    s_saw_data = true;
    uint8_t pci = b[0] >> 4;
    if (pci == 0) {                             /* single frame */
        uint8_t sl = b[0] & 0x0F;
        if (sl >= 1 && sl < nb) {
            cb(id, b + 1, sl, arg);
        }
    } else if (pci == 1 && nb >= 2) {           /* first frame */
        s_asm_id = id;
        s_asm_total = (uint16_t)((b[0] & 0x0F) << 8 | b[1]);
        if (s_asm_total > sizeof(s_asm)) {
            s_asm_total = sizeof(s_asm);
        }
        s_asm_len = 0;
        for (int i = 2; i < nb && s_asm_len < s_asm_total; i++) {
            s_asm[s_asm_len++] = b[i];
        }
        s_asm_sn = 1;
    } else if (pci == 2 && id == s_asm_id && s_asm_total) {     /* consecutive frame */
        if ((b[0] & 0x0F) != (s_asm_sn & 0x0F)) {
            ESP_LOGW(TAG, "frame out of sequence");
            s_asm_total = 0;
            return;
        }
        s_asm_sn++;
        for (int i = 1; i < nb && s_asm_len < s_asm_total; i++) {
            s_asm[s_asm_len++] = b[i];
        }
        if (s_asm_len >= s_asm_total) {
            cb(id, s_asm, s_asm_len, arg);
            s_asm_total = 0;
        }
    }
}

static void on_line(const char *l, obd_elm_msg_cb_t cb, void *arg)
{
    if (l[0] == '\0' || strncmp(l, "SEARCHING", 9) == 0) {
        return;
    }
    if (strcmp(l, "NO DATA") == 0) {
        s_saw_nodata = true;
        return;
    }
    if (isxdigit((unsigned char)l[0]) && hexval(l[1]) >= 0 && hexval(l[2]) >= 0 && strchr(l, ' ') == NULL) {
        on_frame_line(l, cb, arg);
        return;
    }
    if (strcmp(l, "?") == 0 && s_req_frames) {
        s_count_ok = false;             /* this adapter doesn't take the count digit */
        ESP_LOGW(TAG, "adapter rejects the response count: not used any more");
    }
    /* CAN ERROR, BUS INIT..., UNABLE TO CONNECT, BUFFER FULL, STOPPED, ?, <RX ERROR, ... */
    s_saw_error = true;
    snprintf(s_error, sizeof(s_error), "%.23s", l);
    ESP_LOGW(TAG, "adapter: %s", l);
}

obd_elm_result_t obd_elm_poll(uint32_t wait_ms, obd_elm_msg_cb_t cb, void *arg)
{
    if (!s_pending) {
        return OBD_ELM_IDLE;
    }
    if (!obd_elm_ready()) {
        s_pending = false;
        s_inited = false;
        snprintf(s_error, sizeof(s_error), "adapter disconnected");
        return OBD_ELM_ERROR;
    }
    char b[128];
    size_t r = s_io.read(b, sizeof(b), wait_ms);
    for (size_t i = 0; i < r; i++) {
        char c = b[i];
        if (c == '\r' || c == '\n' || c == '>') {
            s_line[s_line_len] = '\0';
            on_line(s_line, cb, arg);
            s_line_len = 0;
            if (c == '>') {                     /* adapter finished this request */
                s_pending = false;
                if (s_req_frames > 1 && s_asm_total && s_asm_len < s_asm_total) {
                    s_count_mf_ok = false;      /* the count cut a multi-frame answer short */
                    ESP_LOGW(TAG, "multi-frame answer cut short: count only for single frames now, resending");
                    if (send_payload(s_last_req, s_last_len, 0) == ESP_OK) {
                        return OBD_ELM_PENDING;
                    }
                    return OBD_ELM_ERROR;
                }
                uint32_t dt = now_ms() - s_req_start;
                s_stat_n++;
                s_stat_sum_ms += dt;
                s_stat_max_ms = dt > s_stat_max_ms ? dt : s_stat_max_ms;
                if (s_stat_n == 100) {
                    ESP_LOGI(TAG, "requests: avg %lu ms, max %lu ms (last 100)",
                             (unsigned long)(s_stat_sum_ms / s_stat_n), (unsigned long)s_stat_max_ms);
                    s_stat_n = s_stat_sum_ms = s_stat_max_ms = 0;
                }
                if (s_saw_error && !s_saw_data) {
                    return OBD_ELM_ERROR;
                }
                return OBD_ELM_PROMPT;
            }
        } else if (s_line_len < LINE_MAX_ - 1 && isprint((unsigned char)c)) {
            s_line[s_line_len++] = c;
        }
    }
    if ((int32_t)(now_ms() - s_deadline) > 0) {
        /* No prompt: the adapter hangs or the link is gone. Set up again next time. */
        s_pending = false;
        s_inited = false;
        snprintf(s_error, sizeof(s_error), "adapter timeout");
        return OBD_ELM_ERROR;
    }
    return OBD_ELM_PENDING;
}

void obd_elm_extend(uint32_t ms)
{
    uint32_t d = now_ms() + ms;
    if ((int32_t)(d - s_deadline) > 0) {
        s_deadline = d;
    }
}

void obd_elm_abort(void)
{
    if (s_pending) {
        /* Any character stops a running ELM command; then wait for its prompt
         * so the next command starts in sync. */
        s_io.write("\r", 1);
        char r[32];
        uint32_t end = now_ms() + 500;
        bool prompt = false;
        while (!prompt && (int32_t)(now_ms() - end) < 0) {
            size_t n = s_io.read(r, sizeof(r), 20);
            prompt = memchr(r, '>', n) != NULL;
        }
        s_pending = false;
        if (!prompt) {
            s_inited = false;
        }
    }
}

/* ---------------------------------------------------------------- monitor (ATMA)
 * Raw frames of a range of IDs, for a CAN broadcast (Dash page). CAF0: all
 * data bytes as received, no ISO-TP interpretation; ATCF / ATCM: only IDs
 * matching filter under mask. Afterwards the adapter is set up again (ATZ)
 * before OBD uses it, so none of this leaks into OBD requests. */

static uint32_t s_mon_filter, s_mon_mask;
static uint32_t s_mon_restarts;

static bool mon_begin(void)
{
    char c[16];
    if (!cmd_ok("ATCAF0")) {
        return false;
    }
    snprintf(c, sizeof(c), "ATCF%03lX", (unsigned long)s_mon_filter);
    if (!cmd_ok(c)) {
        return false;
    }
    snprintf(c, sizeof(c), "ATCM%03lX", (unsigned long)s_mon_mask);
    if (!cmd_ok(c)) {
        return false;
    }
    drain();
    s_line_len = 0;
    return s_io.write("ATMA\r", 5) == ESP_OK;
}

esp_err_t obd_elm_monitor_start(uint32_t filter, uint32_t mask)
{
    if (!obd_elm_ready()) {
        snprintf(s_error, sizeof(s_error), "adapter not connected");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_pending) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_inited && !init_adapter()) {
        return ESP_FAIL;
    }
    s_mon_filter = filter & 0x7FF;
    s_mon_mask = mask & 0x7FF;
    s_inited = false;                   /* OBD sets the adapter up again afterwards */
    if (!mon_begin()) {
        snprintf(s_error, sizeof(s_error), "monitor set-up failed");
        return ESP_FAIL;
    }
    s_mon = true;
    s_error[0] = '\0';
    ESP_LOGI(TAG, "monitoring IDs %03lX / mask %03lX", (unsigned long)s_mon_filter, (unsigned long)s_mon_mask);
    return ESP_OK;
}

bool obd_elm_monitoring(void)
{
    return s_mon;
}

uint32_t obd_elm_monitor_restarts(void)
{
    return s_mon_restarts;
}

/* "2001122334455667788" -> id + up to 8 data bytes */
static void mon_line(const char *l, obd_elm_frame_cb_t cb, void *arg)
{
    size_t n = strlen(l);
    if (n == 0) {
        return;
    }
    if (n < 3 || (n - 3) % 2 != 0 || n > 3 + 16) {
        if (strstr(l, "BUFFER FULL") || strstr(l, "STOPPED") || strchr(l, ' ')) {
            ESP_LOGW(TAG, "monitor: %s", l);
            snprintf(s_error, sizeof(s_error), "%.23s", l);
        }
        return;
    }
    uint32_t id = 0;
    uint8_t d[8];
    for (int i = 0; i < 3; i++) {
        int v = hexval(l[i]);
        if (v < 0) return;
        id = id << 4 | (uint32_t)v;
    }
    uint8_t dlc = 0;
    for (size_t i = 3; i + 1 < n; i += 2) {
        int hi = hexval(l[i]), lo = hexval(l[i + 1]);
        if (hi < 0 || lo < 0) return;
        d[dlc++] = (uint8_t)(hi << 4 | lo);
    }
    cb(id, d, dlc, arg);
}

bool obd_elm_monitor_poll(uint32_t wait_ms, obd_elm_frame_cb_t cb, void *arg)
{
    if (!s_mon) {
        return false;
    }
    if (!obd_elm_ready()) {
        s_mon = false;
        snprintf(s_error, sizeof(s_error), "adapter disconnected");
        return false;
    }
    char b[256];
    size_t r = s_io.read(b, sizeof(b), wait_ms);
    for (size_t i = 0; i < r; i++) {
        char c = b[i];
        if (c == '\r' || c == '\n' || c == '>') {
            s_line[s_line_len] = '\0';
            mon_line(s_line, cb, arg);
            s_line_len = 0;
            if (c == '>') {
                /* the adapter stopped by itself (BUFFER FULL): start again */
                s_mon_restarts++;
                if (!mon_begin()) {
                    s_mon = false;
                    return false;
                }
            }
        } else if (s_line_len < LINE_MAX_ - 1 && isprint((unsigned char)c)) {
            s_line[s_line_len++] = c;
        }
    }
    return true;
}

void obd_elm_monitor_stop(void)
{
    if (!s_mon) {
        return;
    }
    s_mon = false;
    s_io.write("\r", 1);                /* any character stops ATMA */
    char r[64];
    uint32_t end = now_ms() + 500;
    while ((int32_t)(now_ms() - end) < 0) {
        size_t n = s_io.read(r, sizeof(r), 20);
        if (memchr(r, '>', n)) {
            break;
        }
    }
    drain();
    s_inited = false;
    ESP_LOGI(TAG, "monitor stopped (%lu restarts)", (unsigned long)s_mon_restarts);
}
