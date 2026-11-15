/*
 * Wi-Fi page.
 *   wifi_list / wifi_sel   dropdown: scan results, two-way selected index
 *   wifi_pass              password text area (two-way), cleared after use
 *   wifi_autoconnect       checkbox -> settings + wifi_svc auto retry
 *   wifi_scan_text, wifi_status
 */
#include <stdio.h>
#include "esp_attr.h"
#include <string.h>
#include "esp_timer.h"
#include "bridge.h"
#include "wifi_svc.h"
#include "settings.h"
#include "web_svc.h"

static uivar_t v_list = UIVAR("wifi_list");
static uivar_t v_sel = UIVAR("wifi_sel");
static uivar_t v_pass = UIVAR("wifi_pass");
static uivar_t v_scan = UIVAR("wifi_scan_text");
static uivar_t v_status = UIVAR("wifi_status");
static uivar_t v_auto = UIVAR("wifi_autoconnect");

static wifi_svc_ap_t s_aps[WIFI_SVC_MAX_APS];
static int           s_nap;
static uint32_t      s_scan_seen;
static bool          s_auto;
static char          s_msg[64];     /* feedback of the last action */

static const char *state_text(const wifi_svc_status_t *st)
{
    if (st->state == WIFI_SVC_FAILED && st->retry_waiting) {
        return "waiting for the network (retry every 30 s)";
    }
    switch (st->state) {
    case WIFI_SVC_CONNECTING: return "connecting...";
    case WIFI_SVC_CONNECTED:  return "connected";
    case WIFI_SVC_FAILED:     return "failed";
    default:                  return "not connected";
    }
}

static void build_list(const wifi_svc_status_t *st)
{
    EXT_RAM_BSS_ATTR static char opts[WIFI_SVC_MAX_APS * 64];
    s_nap = wifi_svc_get_scan(s_aps, WIFI_SVC_MAX_APS);
    size_t o = 0;
    int sel = 0;
    for (int i = 0; i < s_nap; i++) {
        o += snprintf(opts + o, sizeof(opts) - o, "%s%s   %d dBm  %s",
                      i ? "\n" : "", s_aps[i].ssid, s_aps[i].rssi, s_aps[i].auth);
        if (strcmp(s_aps[i].ssid, st->state == WIFI_SVC_IDLE ? st->saved_ssid : st->ssid) == 0) {
            sel = i;
        }
    }
    uv_str(&v_list, s_nap ? opts : "(no networks found)");
    uv_int(&v_sel, sel);
    uv_strf(&v_scan, "%d networks", s_nap);
}

void view_wifi_init(void)
{
    app_settings_t cfg;
    settings_get(&cfg);
    s_auto = cfg.wifi_autoconnect;
    uv_bool(&v_auto, s_auto);
    uv_str(&v_pass, "");
}

void view_wifi_update(bool visible)
{
    wifi_svc_status_t st;
    wifi_svc_get_status(&st);

    bool a = uv_get_bool(&v_auto, s_auto);
    if (a != s_auto) {
        s_auto = a;
        app_settings_t cfg;
        settings_get(&cfg);
        cfg.wifi_autoconnect = a;
        settings_put(&cfg);
        wifi_svc_set_auto_retry(a);
    }

    if (st.scan_count != s_scan_seen) {
        s_scan_seen = st.scan_count;
        build_list(&st);
    } else if (st.scanning) {
        uv_str(&v_scan, "scanning...");
    }

    if (!visible) {
        return;
    }
    char up[24] = "";
    if (st.state == WIFI_SVC_CONNECTED) {
        fmt_duration((uint32_t)(esp_timer_get_time() / 1000000) - st.connected_since_s, up, sizeof(up));
    }
    char buf[512];
    int o = snprintf(buf, sizeof(buf), "State:  %s\n", state_text(&st));
    if (st.state != WIFI_SVC_IDLE && st.ssid[0]) {
        o += snprintf(buf + o, sizeof(buf) - o, "SSID:  %s\n", st.ssid);
    }
    if (st.state == WIFI_SVC_CONNECTED) {
        o += snprintf(buf + o, sizeof(buf) - o,
                      "IP:  %s\nMask:  %s\nGateway:  %s\nDNS:  %s\nRSSI:  %d dBm   ch %u\nUp:  %s\n",
                      st.ip, st.netmask, st.gw, st.dns, st.rssi, st.channel, up);
        o += snprintf(buf + o, sizeof(buf) - o, "Time:  %s\n", st.time_synced ? "synchronised (SNTP)" : "waiting for SNTP");
        if (web_svc_running()) {
            o += snprintf(buf + o, sizeof(buf) - o, "Log files:  http://%s/\nPC tool PIN:  %s\n", st.ip, web_svc_pin());
        }
    }
    if (st.last_reason && st.state != WIFI_SVC_CONNECTED) {
        o += snprintf(buf + o, sizeof(buf) - o, "Last error:  %u %s\n", st.last_reason, st.last_reason_text);
    }
    o += snprintf(buf + o, sizeof(buf) - o, "Stored:  %s\nMAC:  %s",
                  st.has_saved ? st.saved_ssid : "(none)", st.mac);
    if (s_msg[0]) {
        snprintf(buf + o, sizeof(buf) - o, "\n%s", s_msg);
    }
    uv_str(&v_status, buf);
}

void view_wifi_scan(void)
{
    s_msg[0] = '\0';
    if (wifi_svc_scan() != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "Scan not possible now (connecting?)");
    } else {
        uv_str(&v_scan, "scanning...");
    }
}

void view_wifi_connect(void)
{
    char pass[65];
    uv_get_str(&v_pass, pass, sizeof(pass));
    int sel = uv_get_int(&v_sel, 0);
    esp_err_t err;
    s_msg[0] = '\0';
    if (sel >= 0 && sel < s_nap) {
        if (s_aps[sel].secure && pass[0] == '\0') {
            wifi_svc_status_t st;
            wifi_svc_get_status(&st);
            if (st.has_saved && strcmp(st.saved_ssid, s_aps[sel].ssid) == 0) {
                err = wifi_svc_connect_saved();     /* stored password */
            } else {
                snprintf(s_msg, sizeof(s_msg), "%s needs a password", s_aps[sel].ssid);
                return;
            }
        } else {
            err = wifi_svc_connect(s_aps[sel].ssid, pass);
        }
    } else {
        err = wifi_svc_connect_saved();
        if (err == ESP_ERR_NOT_FOUND) {
            snprintf(s_msg, sizeof(s_msg), "Scan and pick a network first");
            return;
        }
    }
    if (err != ESP_OK) {
        snprintf(s_msg, sizeof(s_msg), "Connect failed: %s", esp_err_to_name(err));
    }
    uv_str(&v_pass, "");
}

void view_wifi_disconnect(void)
{
    s_msg[0] = '\0';
    wifi_svc_disconnect();
}

void view_wifi_forget(void)
{
    wifi_svc_forget();
    snprintf(s_msg, sizeof(s_msg), "Stored network erased");
}
