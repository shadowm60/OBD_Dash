/*
 * BLE adapter console (page ble_console): raw text to / from the serial port
 * of the connected BLE device (ble_svc_serial_*), e.g. an ELM327 OBD adapter.
 *   elm_log    last lines, "> " = sent, the rest = received
 *   elm_info   serial port / state
 *   elm_cmd    text area for a custom command (two-way)
 * Every line also goes to the serial log (tag view_elm), for copy / paste.
 * Commands get the ELM327 terminator '\r'. Step A1 of the roadmap (CLAUDE.md):
 * proves the BLE link before OBD uses it.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "bridge.h"
#include "ble_svc.h"
#include "comsm.h"
#include "settings.h"
#include "obd_client.h"

static const char *TAG = "view_elm";

#define LINES     19            /* what fits the log panel at 16 px */
#define LINE_LEN  64

static uivar_t v_log = UIVAR("elm_log");
static uivar_t v_info = UIVAR("elm_info");
static uivar_t v_cmd = UIVAR("elm_cmd");

EXT_RAM_BSS_ATTR static char s_lines[LINES][LINE_LEN];
static int  s_head, s_count;            /* ring of finished lines */
static char s_cur[LINE_LEN];            /* line being received */
static int  s_cur_len;
static bool s_dirty = true;
static bool s_last_empty;

static void push_line(const char *text)
{
    bool empty = text[0] == '\0';
    if (empty && (s_last_empty || s_count == 0)) {
        return;                         /* ELM sends "\r\r>": no runs of blank lines */
    }
    s_last_empty = empty;
    if (!empty) {
        ESP_LOGI(TAG, "%s", text);      /* the console in the serial log too, for copy / paste */
    }
    strlcpy(s_lines[s_head], text, LINE_LEN);
    s_head = (s_head + 1) % LINES;
    if (s_count < LINES) {
        s_count++;
    }
    s_dirty = true;
}

static void flush_cur(void)
{
    s_cur[s_cur_len] = '\0';
    push_line(s_cur);
    s_cur_len = 0;
}

static void rx_bytes(const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        char c = p[i];
        if (c == '\r' || c == '\n') {
            flush_cur();
        } else if (c == '>') {          /* prompt: adapter ready for the next command */
            if (s_cur_len) {
                flush_cur();
            }
            push_line(">");
        } else if (isprint((unsigned char)c) && s_cur_len < LINE_LEN - 1) {
            s_cur[s_cur_len++] = c;
            s_dirty = true;
        }
    }
}

/* OBD is using the adapter: the console must neither read its replies nor send. */
static bool obd_owns_adapter(void)
{
    comsm_status_t cs;
    comsm_get_status(&cs);
    return cs.state != COMSM_IDLE && obd_client_link() == OBD_LINK_ELM;
}

static void send(const char *cmd)
{
    char line[LINE_LEN];
    if (obd_owns_adapter()) {
        push_line("(OBD is using the adapter: Disconnect on the OBD page first)");
        return;
    }
    if (!ble_svc_serial_ready()) {
        push_line("(no serial port: connect the adapter on the BLE page)");
        return;
    }
    snprintf(line, sizeof(line), "> %s", cmd);
    if (s_cur_len) {
        flush_cur();
    }
    push_line(line);
    char tx[LINE_LEN + 2];
    int n = snprintf(tx, sizeof(tx), "%s\r", cmd);
    if (ble_svc_serial_write(tx, (size_t)n) != ESP_OK) {
        push_line("(send failed)");
    }
    obd_client_elm_invalidate();        /* OBD sets the adapter up again before using it */
}

void view_elm_send(const char *cmd)
{
    send(cmd);
}

void view_elm_send_input(void)
{
    char cmd[48];
    uv_get_str(&v_cmd, cmd, sizeof(cmd));
    /* ELM327 commands are case-insensitive; trim and upper-case for the log */
    size_t n = strlen(cmd);
    while (n && cmd[n - 1] == ' ') {
        cmd[--n] = '\0';
    }
    for (size_t i = 0; i < n; i++) {
        cmd[i] = (char)toupper((unsigned char)cmd[i]);
    }
    if (n) {
        send(cmd);
    }
}

void view_elm_clear(void)
{
    s_head = s_count = 0;
    s_cur_len = 0;
    s_last_empty = false;
    s_dirty = true;
}

/* 250 ms, always: keeps the receive buffer drained. */
void view_elm_update(bool visible)
{
    char buf[256];
    size_t n;
    bool obd = obd_owns_adapter();
    while (!obd && (n = ble_svc_serial_read(buf, sizeof(buf), 0)) > 0) {
        rx_bytes(buf, n);
    }
    if (!visible) {
        return;
    }

    static ble_svc_status_t st;     /* LVGL task only, ~1.5 KB */
    ble_svc_get_status(&st);
    if (st.serial_ready && obd) {
        uv_strf(&v_info, "%s\nin use by OBD", st.peer_name[0] ? st.peer_name : st.peer_addr);
    } else if (st.serial_ready) {
        uv_strf(&v_info, "%s\nserial: %s", st.peer_name[0] ? st.peer_name : st.peer_addr, st.serial_info);
    } else if (st.state == BLE_SVC_CONNECTED) {
        uv_str(&v_info, st.svc_done ? "connected, no serial port found" : "connected, looking for the serial port...");
    } else {
        uv_str(&v_info, "not connected: connect the adapter on the BLE page");
    }

    if (!s_dirty) {
        return;
    }
    s_dirty = false;
    static char text[LINES * (LINE_LEN + 1) + LINE_LEN + 2];
    size_t o = 0;
    for (int i = 0; i < s_count; i++) {
        int idx = (s_head - s_count + i + LINES) % LINES;
        o += snprintf(text + o, sizeof(text) - o, "%s%s", i ? "\n" : "", s_lines[idx]);
    }
    if (s_cur_len) {
        s_cur[s_cur_len] = '\0';
        snprintf(text + o, sizeof(text) - o, "%s%s", o ? "\n" : "", s_cur);
    }
    uv_str(&v_log, s_count || s_cur_len ? text : "Press ATZ to reset the adapter, ATI for its version, "
                                                  "ATRV for the battery voltage, 0100 for the PIDs the car supports "
                                                  "(ignition on; after ATZ the adapter picks the protocol itself).");
}
