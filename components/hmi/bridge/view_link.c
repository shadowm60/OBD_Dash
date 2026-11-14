/*
 * Car interface panel (IO page): which link OBD and the Dash use, and the
 * receive filter for listening (CAN monitor, raw log, Dash).
 *   obd_link_sel     Board CAN / BLE adapter (setting obd_link, applied in view_obd.c)
 *   link_filter_sel  LF_* preset, link_filter_id / link_filter_mask (hex, custom)
 *   link_status      what the link is doing
 *
 * BLE adapter: the last connected device with a serial port is remembered
 * (settings ble_adapter_*); with the BLE adapter chosen, ble_svc reconnects
 * to it by itself (every 5 s while nothing is connected).
 *
 * The filter never touches OBD: board CAN = software filter of the monitor
 * taps (can_if_set_monitor_filter), BLE adapter = the adapter's ATCF / ATCM
 * while it monitors for the Dash (view_link_dash_filter).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge.h"
#include "settings.h"
#include "can_if.h"
#include "comsm.h"
#include "ble_svc.h"
#include "dash_svc.h"
#include "obd_client.h"

enum { LF_AUTO, LF_ALL, LF_OBD, LF_DBC, LF_CUSTOM, LF_COUNT };      /* tools/obd_ui.py page_io */

static uivar_t v_sel = UIVAR("link_filter_sel");
static uivar_t v_id = UIVAR("link_filter_id");
static uivar_t v_mask = UIVAR("link_filter_mask");
static uivar_t v_status = UIVAR("link_status");

static int      s_sel = -1;
static uint32_t s_board_id = UINT32_MAX, s_board_mask = UINT32_MAX;     /* applied to can_if */

/* Filter of a preset; false = "everything" (no filter). dash: for the Dash's
 * adapter monitor, where Auto means the DBC's IDs. */
static bool preset_filter(const app_settings_t *c, bool dash, uint32_t *id, uint32_t *mask)
{
    switch (c->link_filter) {
    case LF_ALL:
        return false;
    case LF_OBD:
        *id = 0x7E0;
        *mask = 0x7F0;
        return true;
    case LF_DBC:
        return dash_svc_id_filter(id, mask);
    case LF_CUSTOM:
        *id = c->link_filter_id & 0x7FF;
        *mask = c->link_filter_mask & 0x7FF;
        return *mask != 0;
    default:        /* LF_AUTO */
        return dash && dash_svc_id_filter(id, mask);
    }
}

bool view_link_dash_filter(uint32_t *id, uint32_t *mask)
{
    app_settings_t c;
    settings_get(&c);
    if (!preset_filter(&c, true, id, mask)) {
        *id = 0;
        *mask = 0;                          /* adapter: ATCF 000 / ATCM 000 = everything */
    }
    return true;
}

static void apply_board(const app_settings_t *c)
{
    uint32_t id = 0, mask = 0;
    if (!preset_filter(c, false, &id, &mask)) {
        id = mask = 0;
    }
    if (id != s_board_id || mask != s_board_mask) {
        s_board_id = id;
        s_board_mask = mask;
        can_if_set_monitor_filter(id, mask);
    }
}

void view_link_init(void)
{
    app_settings_t c;
    settings_get(&c);
    s_sel = c.link_filter < LF_COUNT ? c.link_filter : LF_AUTO;
    uv_int(&v_sel, s_sel);
    uv_strf(&v_id, "%03X", c.link_filter_id & 0x7FF);
    uv_strf(&v_mask, "%03X", c.link_filter_mask & 0x7FF);
    apply_board(&c);
}

static bool parse_hex(uivar_t *v, uint16_t *out)
{
    char t[8];
    uv_get_str(v, t, sizeof(t));
    char *end;
    unsigned long x = strtoul(t, &end, 16);
    if (t[0] == '\0' || *end != '\0' || x > 0x7FF) {
        return false;
    }
    *out = (uint16_t)x;
    return true;
}

static void read_inputs(void)
{
    app_settings_t c;
    settings_get(&c);
    bool changed = false;
    int sel = uv_get_int(&v_sel, s_sel);
    if (sel >= 0 && sel < LF_COUNT && sel != c.link_filter) {
        c.link_filter = (uint8_t)sel;
        changed = true;
    }
    s_sel = sel;
    uint16_t id, mask;
    if (parse_hex(&v_id, &id) && id != c.link_filter_id) {
        c.link_filter_id = id;
        changed = true;
    }
    if (parse_hex(&v_mask, &mask) && mask != c.link_filter_mask) {
        c.link_filter_mask = mask;
        changed = true;
    }
    if (changed) {
        settings_put(&c);
    }
    apply_board(&c);                    /* also follows a new DBC / base for LF_DBC */
}

static void status_text(void)
{
    app_settings_t c;
    settings_get(&c);
    comsm_status_t cs;
    comsm_get_status(&cs);
    char filt[40];
    uint32_t id, mask;
    if (c.link_filter == LF_AUTO) {
        snprintf(filt, sizeof(filt), "Auto (Dash: DBC IDs)");
    } else if (preset_filter(&c, true, &id, &mask)) {
        snprintf(filt, sizeof(filt), "ID %03lX, mask %03lX", (unsigned long)id, (unsigned long)mask);
    } else {
        snprintf(filt, sizeof(filt), c.link_filter == LF_DBC ? "no DBC loaded: everything" : "everything");
    }

    if (c.obd_link == OBD_LINK_ELM) {
        static ble_svc_status_t b;      /* LVGL task only */
        ble_svc_get_status(&b);
        const char *use = cs.state != COMSM_IDLE ? "used by OBD" : cs.monitoring ? "listening for the Dash" : "idle";
        if (b.serial_ready) {
            uv_strf(&v_status, "BLE adapter %s: %s.\nFilter: %s", b.peer_name[0] ? b.peer_name : b.peer_addr,
                    use, filt);
        } else if (c.ble_adapter_valid && !ble_svc_autoconnect_paused()) {
            uv_strf(&v_status, "BLE adapter %s: not connected, reconnecting by itself...\nFilter: %s",
                    c.ble_adapter_name[0] ? c.ble_adapter_name : "(remembered)", filt);
        } else if (c.ble_adapter_valid) {
            uv_strf(&v_status, "BLE adapter %s: Disconnect pressed, connect it again on the BLE page.\nFilter: %s",
                    c.ble_adapter_name[0] ? c.ble_adapter_name : "", filt);
        } else {
            uv_strf(&v_status, "BLE adapter not connected: connect it once on the BLE page.\nFilter: %s", filt);
        }
    } else {
        can_if_config_t cc;
        can_if_get_config(&cc);
        uv_strf(&v_status, "Board CAN, %lu kbit/s%s (bitrate on the CAN page).\nFilter (monitor, log, Dash): %s",
                (unsigned long)(cc.bitrate / 1000), cc.listen_only ? ", listen only" : "", filt);
    }
}

/* remember the adapter, keep auto-connect in step with the interface choice */
static void adapter_auto(void)
{
    static ble_svc_status_t b;          /* LVGL task only */
    ble_svc_get_status(&b);
    app_settings_t c;
    settings_get(&c);
    if (b.serial_ready && (!c.ble_adapter_valid || memcmp(c.ble_adapter_addr, b.peer_addr_raw, 6) != 0 ||
                           strncmp(c.ble_adapter_name, b.peer_name, sizeof(c.ble_adapter_name) - 1) != 0)) {
        c.ble_adapter_valid = true;
        memcpy(c.ble_adapter_addr, b.peer_addr_raw, 6);
        c.ble_adapter_type = b.peer_addr_type;
        strlcpy(c.ble_adapter_name, b.peer_name, sizeof(c.ble_adapter_name));
        settings_put(&c);
    }
    bool on = c.obd_link == OBD_LINK_ELM && c.ble_adapter_valid;
    ble_svc_set_autoconnect(on, on ? c.ble_adapter_addr : NULL, c.ble_adapter_type, c.ble_adapter_name);
}

void view_link_update(bool visible)
{
    adapter_auto();
    read_inputs();
    if (visible) {
        status_text();
    }
}
