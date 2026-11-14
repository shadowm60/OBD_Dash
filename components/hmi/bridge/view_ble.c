/*
 * BLE page.
 *   ble_list / ble_sel   dropdown: scan results, two-way selected index
 *   ble_scan_text, ble_status, ble_services
 */
#include <stdio.h>
#include "esp_attr.h"
#include <string.h>
#include "bridge.h"
#include "ble_svc.h"

#define SCAN_SECONDS 10

static uivar_t v_list = UIVAR("ble_list");
static uivar_t v_sel = UIVAR("ble_sel");
static uivar_t v_scan = UIVAR("ble_scan_text");
static uivar_t v_status = UIVAR("ble_status");
static uivar_t v_svcs = UIVAR("ble_services");

static uint32_t s_scan_seen;
static int      s_ndev_shown = -1;
static char     s_msg[64];

static const char *state_text(ble_svc_state_t s)
{
    switch (s) {
    case BLE_SVC_READY:      return "ready";
    case BLE_SVC_SCANNING:   return "scanning";
    case BLE_SVC_CONNECTING: return "connecting";
    case BLE_SVC_CONNECTED:  return "connected";
    default:                 return "starting...";
    }
}

static void build_list(void)
{
    EXT_RAM_BSS_ATTR static ble_svc_dev_t devs[BLE_SVC_MAX_DEVS];
    EXT_RAM_BSS_ATTR static char opts[BLE_SVC_MAX_DEVS * 64];
    int n = ble_svc_get_devices(devs, BLE_SVC_MAX_DEVS);
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        o += snprintf(opts + o, sizeof(opts) - o, "%s%s   %s  %d dBm%s",
                      i ? "\n" : "", devs[i].name[0] ? devs[i].name : "(no name)",
                      devs[i].addr_str, devs[i].rssi, devs[i].connectable ? "" : "  (not connectable)");
    }
    uv_str(&v_list, n ? opts : "(no devices found)");
    s_ndev_shown = n;
}

void view_ble_init(void)
{
}

void view_ble_update(bool visible)
{
    static ble_svc_status_t st;     /* ~1.5 KB: off the LVGL stack */
    ble_svc_get_status(&st);

    /* Grow the list while scanning (new devices only, so an open dropdown
     * isn't reset every 250 ms), final version when the scan ends. */
    if (st.scan_count != s_scan_seen) {
        s_scan_seen = st.scan_count;
        build_list();
        uv_strf(&v_scan, "%d devices", st.ndev);
    } else if (st.state == BLE_SVC_SCANNING) {
        if (st.ndev != s_ndev_shown) {
            build_list();
        }
        uv_strf(&v_scan, "scanning... %d devices", st.ndev);
    }

    if (!visible) {
        return;
    }
    char buf[400];
    int o = snprintf(buf, sizeof(buf), "State:  %s\nBonds stored:  %d\n", state_text(st.state), st.nbonds);
    if (st.state == BLE_SVC_CONNECTED || st.state == BLE_SVC_CONNECTING) {
        o += snprintf(buf + o, sizeof(buf) - o, "\nPeer:  %s\n%s\nRSSI:  %d dBm\n",
                      st.peer_name[0] ? st.peer_name : "(no name)", st.peer_addr, st.peer_rssi);
        if (st.state == BLE_SVC_CONNECTED) {
            /* ELM327 adapters encrypt without storing keys: that is fine. */
            o += snprintf(buf + o, sizeof(buf) - o, "Encrypted:  %s\nBonded:  %s\n",
                          st.encrypted ? "yes" : "no",
                          st.bonded ? "yes" : st.encrypted ? "no (device keeps no bond, not needed)" : "no");
        }
        if (st.passkey) {
            o += snprintf(buf + o, sizeof(buf) - o, "Code:  %06u\n", (unsigned)st.passkey);
        }
    }
    if (st.last_event[0]) {
        o += snprintf(buf + o, sizeof(buf) - o, "\n%s", st.last_event);
    }
    if (s_msg[0]) {
        snprintf(buf + o, sizeof(buf) - o, "\n%s", s_msg);
    }
    uv_str(&v_status, buf);

    if (st.state == BLE_SVC_CONNECTED) {
        static char s[BLE_SVC_MAX_SVCS * 42 + 32];
        size_t k = 0;
        for (int i = 0; i < st.nsvc; i++) {
            k += snprintf(s + k, sizeof(s) - k, "%s\n", st.svcs[i]);
        }
        if (!st.svc_done) {
            snprintf(s + k, sizeof(s) - k, "discovering...");
        } else if (st.nsvc == 0) {
            snprintf(s + k, sizeof(s) - k, "(none)");
        }
        uv_str(&v_svcs, s);
    } else {
        uv_str(&v_svcs, "connect to a device to list its services");
    }
}

void view_ble_scan(void)
{
    s_msg[0] = '\0';
    if (ble_svc_scan(SCAN_SECONDS) != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "Scan not possible now (connected?)");
    }
    s_ndev_shown = -1;
}

void view_ble_connect(void)
{
    s_msg[0] = '\0';
    static ble_svc_status_t st;     /* LVGL task only */
    ble_svc_get_status(&st);
    int sel = uv_get_int(&v_sel, 0);
    if (st.ndev == 0) {
        snprintf(s_msg, sizeof(s_msg), "Scan and pick a device first");
    } else if (st.state == BLE_SVC_CONNECTED || st.state == BLE_SVC_CONNECTING) {
        snprintf(s_msg, sizeof(s_msg), "Already %s: Disconnect first",
                 st.state == BLE_SVC_CONNECTED ? "connected" : "connecting");
    } else if (st.state == BLE_SVC_OFF) {
        snprintf(s_msg, sizeof(s_msg), "Bluetooth is still starting");
    } else if (ble_svc_connect(sel) != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "Connect failed, see the log (scan again?)");
    }
}

void view_ble_disconnect(void)
{
    s_msg[0] = '\0';
    ble_svc_disconnect();
}

void view_ble_forget(void)
{
    s_msg[0] = '\0';
    ble_svc_forget_all();
}
