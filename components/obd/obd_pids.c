#include "obd_pids.h"
#include "obd_pids_priv.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"

static const char *TAG = "obd_pids";

#define MAX_CATALOGUE 64

static const uint32_t s_rate_period_ms[] = {
    [OBD_RATE_FAST]   = 0,
    [OBD_RATE_MEDIUM] = 500,
    [OBD_RATE_SLOW]   = 2000,
};

static uint8_t      s_supported[256 / 8];           /* bitmap over all PIDs */
static bool         s_enabled[MAX_CATALOGUE];       /* user selection, default off */
static uint32_t     s_last_poll[MAX_CATALOGUE];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static inline bool bit_get(uint8_t pid)  { return (s_supported[pid >> 3] >> (pid & 7)) & 1; }
static inline void bit_set(uint8_t pid)  { s_supported[pid >> 3] |= (uint8_t)(1u << (pid & 7)); }
static inline void bit_clr(uint8_t pid)  { s_supported[pid >> 3] &= (uint8_t)~(1u << (pid & 7)); }

size_t obd_pid_count(void)
{
    return g_obd_pid_count < MAX_CATALOGUE ? g_obd_pid_count : MAX_CATALOGUE;
}

const obd_pid_desc_t *obd_pid_desc(size_t idx)
{
    return idx < obd_pid_count() ? &g_obd_pids[idx] : NULL;
}

int obd_pid_find(uint8_t pid)
{
    for (size_t i = 0; i < obd_pid_count(); i++) {
        if (g_obd_pids[i].pid == pid) {
            return (int)i;
        }
    }
    return -1;
}

void obd_pid_reset_supported(void)
{
    portENTER_CRITICAL(&s_lock);
    memset(s_supported, 0, sizeof(s_supported));
    memset(s_last_poll, 0, sizeof(s_last_poll));
    portEXIT_CRITICAL(&s_lock);
}

bool obd_pid_apply_support_bitmap(uint8_t base_pid, uint32_t bitmap)
{
    portENTER_CRITICAL(&s_lock);
    for (int i = 0; i < 32; i++) {
        int pid = base_pid + 1 + i;
        if (pid > 0xFF) {
            break;
        }
        if (bitmap & (1u << (31 - i))) {
            bit_set((uint8_t)pid);
        }
    }
    portEXIT_CRITICAL(&s_lock);

    /* Last bit = "PID base+0x20 supported", i.e. there is another range. */
    return (bitmap & 1u) != 0 && base_pid < 0xE0;
}

void obd_pid_mark_unsupported(uint8_t pid)
{
    portENTER_CRITICAL(&s_lock);
    bit_clr(pid);
    portEXIT_CRITICAL(&s_lock);
    ESP_LOGW(TAG, "PID 0x%02X rejected by ECU, removed from polling", pid);
}

bool obd_pid_is_supported(uint8_t pid)
{
    portENTER_CRITICAL(&s_lock);
    bool s = bit_get(pid);
    portEXIT_CRITICAL(&s_lock);
    return s;
}

uint16_t obd_pid_supported_count(void)
{
    uint16_t n = 0;
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < obd_pid_count(); i++) {
        n += bit_get(g_obd_pids[i].pid);
    }
    portEXIT_CRITICAL(&s_lock);
    return n;
}

void obd_pid_get_supported_bitmap(uint8_t out[256 / 8])
{
    portENTER_CRITICAL(&s_lock);
    memcpy(out, s_supported, sizeof(s_supported));
    portEXIT_CRITICAL(&s_lock);
}

uint16_t obd_pid_active_count(void)
{
    uint16_t n = 0;
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < obd_pid_count(); i++) {
        n += s_enabled[i] && bit_get(g_obd_pids[i].pid);
    }
    portEXIT_CRITICAL(&s_lock);
    return n;
}

bool obd_pid_is_enabled(size_t idx)
{
    if (idx >= obd_pid_count()) {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    bool e = s_enabled[idx];
    portEXIT_CRITICAL(&s_lock);
    return e;
}

void obd_pid_set_enabled(size_t idx, bool enabled)
{
    if (idx >= obd_pid_count()) {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_enabled[idx] = enabled;
    portEXIT_CRITICAL(&s_lock);
    if (!enabled) {
        vd_invalidate(g_obd_pids[idx].signal);
    }
}

/*
 * Pick the next PIDs to request (up to `max` in one Service 01 request).
 *
 * Fast PIDs (period 0) rotate round-robin by last poll time. Medium / slow PIDs
 * that are due go first, so they keep their period however many fast PIDs are
 * selected - but they get at most half of the slots (with a single PID per
 * request: every other request), so a long list of due medium PIDs can't
 * starve the fast ones (rpm, speed). Unused slots go to whatever is due.
 */
size_t obd_pid_next_batch(uint32_t now_ms, size_t max, int out[])
{
    static bool s_last_was_timed;           /* alternation for max == 1 */
    int32_t overdue[MAX_CATALOGUE];
    bool    timed[MAX_CATALOGUE];
    size_t  n_fast = 0, n_timed = 0;

    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0; i < obd_pid_count(); i++) {
        const obd_pid_desc_t *d = &g_obd_pids[i];
        overdue[i] = -1;
        if (!s_enabled[i] || !bit_get(d->pid)) {
            continue;
        }
        int32_t o = (int32_t)(now_ms - s_last_poll[i]) - (int32_t)s_rate_period_ms[d->rate];
        if (o < 0) {
            continue;
        }
        overdue[i] = o;
        timed[i] = d->rate != OBD_RATE_FAST;
        if (timed[i]) {
            n_timed++;
        } else {
            n_fast++;
        }
    }
    portEXIT_CRITICAL(&s_lock);

    size_t quota;                           /* slots reserved for timed PIDs */
    if (n_fast == 0) {
        quota = max;
    } else if (max == 1) {
        quota = s_last_was_timed ? 0 : 1;
    } else {
        quota = (max + 1) / 2;
    }

    size_t n = 0;
    /* pass 0: timed within quota, pass 1: fast, pass 2: leftovers of either */
    for (int pass = 0; pass < 3 && n < max; pass++) {
        while (n < max) {
            if (pass == 0 && n >= quota) {
                break;
            }
            int best = -1;
            for (size_t i = 0; i < obd_pid_count(); i++) {
                if (overdue[i] < 0 || (pass == 0 && !timed[i]) || (pass == 1 && timed[i])) {
                    continue;
                }
                if (best < 0 || overdue[i] > overdue[best]) {
                    best = (int)i;
                }
            }
            if (best < 0) {
                break;
            }
            out[n++] = best;
            overdue[best] = -1;             /* taken */
        }
    }
    if (max == 1) {
        s_last_was_timed = (n == 1 && timed[out[0]]);
    }
    return n;
}

void obd_pid_mark_polled(size_t idx, uint32_t now_ms)
{
    if (idx < obd_pid_count()) {
        portENTER_CRITICAL(&s_lock);
        s_last_poll[idx] = now_ms;
        portEXIT_CRITICAL(&s_lock);
    }
}

size_t obd_pid_handle_response(const uint8_t *resp, uint16_t len)
{
    if (len < 2 || resp[0] != 0x41) {
        return 0;
    }
    /* 41 PID data [PID data ...] - a multi-PID answer repeats PID + data. */
    size_t count = 0;
    uint16_t i = 1;
    while (i < len) {
        int idx = obd_pid_find(resp[i]);
        if (idx < 0) {
            break;                          /* unknown length: can't go on */
        }
        const obd_pid_desc_t *d = &g_obd_pids[idx];
        if (i + 1 + d->len > len) {
            ESP_LOGW(TAG, "PID 0x%02X: short response (%u bytes)", d->pid, len);
            break;
        }
        vd_set(d->signal, d->decode(&resp[i + 1]));
        count++;
        i += 1 + d->len;
    }
    return count;
}
