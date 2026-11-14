#include "wifi_svc.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_svc";

#define MAX_RETRIES     5       /* then FAILED until the user tries again */

static SemaphoreHandle_t s_lock;
static esp_netif_t      *s_netif;
static wifi_svc_status_t s_st;
static wifi_svc_ap_t     s_aps[WIFI_SVC_MAX_APS];
static int               s_nap;
static bool              s_want_connected;      /* user wants a connection */
static int               s_retries;
static bool              s_sntp_started;
static wifi_svc_sync_cb_t s_sync_cb;

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static const char *auth_name(wifi_auth_mode_t m)
{
    switch (m) {
    case WIFI_AUTH_OPEN:            return "open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    default:                        return "other";
    }
}

static const char *reason_text(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:     return "wrong password?";
    case WIFI_REASON_NO_AP_FOUND:           return "network not found";
    case WIFI_REASON_ASSOC_LEAVE:           return "disconnected";
    case WIFI_REASON_BEACON_TIMEOUT:        return "signal lost";
    case WIFI_REASON_AUTH_EXPIRE:           return "auth expired";
    case WIFI_REASON_CONNECTION_FAIL:       return "connection failed";
    default:                                return "";
    }
}

static void refresh_saved(void)
{
    wifi_config_t cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != '\0') {
        s_st.has_saved = true;
        snprintf(s_st.saved_ssid, sizeof(s_st.saved_ssid), "%s", (const char *)cfg.sta.ssid);
    } else {
        s_st.has_saved = false;
        s_st.saved_ssid[0] = '\0';
    }
}

static void time_sync_cb(struct timeval *tv)
{
    LOCK();
    s_st.time_synced = true;
    UNLOCK();
    time_t now = tv->tv_sec;
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    ESP_LOGI(TAG, "time synchronised: %s", buf);
    if (s_sync_cb) {
        s_sync_cb();
    }
}

void wifi_svc_set_sync_cb(wifi_svc_sync_cb_t cb)
{
    s_sync_cb = cb;
}

static void start_sntp(void)
{
    if (s_sntp_started) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_APP_NTP_SERVER);
    cfg.sync_cb = time_sync_cb;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        s_sntp_started = true;
    }
}

static void on_scan_done(void)
{
    uint16_t n = WIFI_SVC_MAX_APS;
    wifi_ap_record_t *rec = calloc(WIFI_SVC_MAX_APS, sizeof(*rec));
    if (rec == NULL) {
        esp_wifi_clear_ap_list();
        return;
    }
    if (esp_wifi_scan_get_ap_records(&n, rec) != ESP_OK) {
        n = 0;
    }
    LOCK();
    s_nap = 0;
    for (int i = 0; i < n; i++) {
        if (rec[i].ssid[0] == '\0') {
            continue;               /* hidden network */
        }
        /* The driver can report the same SSID per AP / band: keep the strongest. */
        bool dup = false;
        for (int j = 0; j < s_nap; j++) {
            if (strcmp(s_aps[j].ssid, (const char *)rec[i].ssid) == 0) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        wifi_svc_ap_t *a = &s_aps[s_nap++];
        snprintf(a->ssid, sizeof(a->ssid), "%s", (const char *)rec[i].ssid);
        a->rssi = rec[i].rssi;
        a->channel = rec[i].primary;
        a->secure = rec[i].authmode != WIFI_AUTH_OPEN;
        snprintf(a->auth, sizeof(a->auth), "%s", auth_name(rec[i].authmode));
    }
    s_st.scanning = false;
    s_st.scan_count++;
    UNLOCK();
    free(rec);
    ESP_LOGI(TAG, "scan done: %d networks", s_nap);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_want_connected) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_SCAN_DONE:
            on_scan_done();
            break;
        case WIFI_EVENT_STA_CONNECTED: {
            const wifi_event_sta_connected_t *e = data;
            LOCK();
            snprintf(s_st.ssid, sizeof(s_st.ssid), "%.*s", e->ssid_len, (const char *)e->ssid);
            s_st.channel = e->channel;
            UNLOCK();
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *e = data;
            LOCK();
            s_st.ip[0] = s_st.gw[0] = s_st.netmask[0] = s_st.dns[0] = '\0';
            s_st.last_reason = e->reason;
            snprintf(s_st.last_reason_text, sizeof(s_st.last_reason_text), "%s", reason_text(e->reason));
            if (!s_want_connected) {
                s_st.state = WIFI_SVC_IDLE;
            } else if (s_retries < MAX_RETRIES) {
                s_retries++;
                s_st.state = WIFI_SVC_CONNECTING;
            } else {
                s_st.state = WIFI_SVC_FAILED;
                s_want_connected = false;
            }
            bool retry = s_want_connected;
            UNLOCK();
            ESP_LOGW(TAG, "disconnected, reason %u %s%s", e->reason, reason_text(e->reason),
                     retry ? ", retrying" : "");
            if (retry) {
                esp_wifi_connect();
            }
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        esp_netif_dns_info_t dns = { 0 };
        esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns);
        LOCK();
        snprintf(s_st.ip, sizeof(s_st.ip), IPSTR, IP2STR(&e->ip_info.ip));
        snprintf(s_st.gw, sizeof(s_st.gw), IPSTR, IP2STR(&e->ip_info.gw));
        snprintf(s_st.netmask, sizeof(s_st.netmask), IPSTR, IP2STR(&e->ip_info.netmask));
        snprintf(s_st.dns, sizeof(s_st.dns), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        s_st.state = WIFI_SVC_CONNECTED;
        s_st.connected_since_s = uptime_s();
        s_st.last_reason = 0;
        s_st.last_reason_text[0] = '\0';
        s_retries = 0;
        refresh_saved();
        UNLOCK();
        ESP_LOGI(TAG, "got IP %s", s_st.ip);
        start_sntp();
    }
}

esp_err_t wifi_svc_init(bool autoconnect)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }
    setenv("TZ", CONFIG_APP_TZ, 1);
    tzset();

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(s_netif, "obd-dash");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL), TAG, "evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL), TAG, "evt");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_FLASH), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");

    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    LOCK();
    snprintf(s_st.mac, sizeof(s_st.mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    refresh_saved();
    s_want_connected = autoconnect && s_st.has_saved;
    s_st.state = s_want_connected ? WIFI_SVC_CONNECTING : WIFI_SVC_IDLE;
    if (s_want_connected) {
        snprintf(s_st.ssid, sizeof(s_st.ssid), "%s", s_st.saved_ssid);
    }
    UNLOCK();

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");    /* STA_START -> connect */
    ESP_LOGI(TAG, "started%s%s", s_want_connected ? ", connecting to " : "",
             s_want_connected ? s_st.saved_ssid : "");
    return ESP_OK;
}

esp_err_t wifi_svc_scan(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    LOCK();
    bool busy = s_st.scanning;
    UNLOCK();
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }
    const wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err == ESP_OK) {
        LOCK();
        s_st.scanning = true;
        UNLOCK();
    } else {
        ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t wifi_svc_connect(const char *ssid, const char *password)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    if (ssid == NULL || ssid[0] == '\0' || strlen(ssid) > 32 || (password && strlen(password) > 63)) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_config_t cfg = { 0 };
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    if (password != NULL && password[0] != '\0') {
        memcpy(cfg.sta.password, password, strlen(password));
        cfg.sta.threshold.authmode = WIFI_AUTH_WEP;     /* accept anything with a key */
    } else {
        cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    LOCK();
    s_want_connected = false;       /* the disconnect below must not retry */
    UNLOCK();
    esp_wifi_disconnect();
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "config");

    LOCK();
    s_want_connected = true;
    s_retries = 0;
    s_st.state = WIFI_SVC_CONNECTING;
    snprintf(s_st.ssid, sizeof(s_st.ssid), "%s", ssid);
    refresh_saved();
    UNLOCK();
    ESP_LOGI(TAG, "connecting to %s", ssid);
    return esp_wifi_connect();
}

esp_err_t wifi_svc_connect_saved(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    LOCK();
    bool has = s_st.has_saved;
    if (has) {
        s_want_connected = true;
        s_retries = 0;
        s_st.state = WIFI_SVC_CONNECTING;
        snprintf(s_st.ssid, sizeof(s_st.ssid), "%s", s_st.saved_ssid);
    }
    UNLOCK();
    return has ? esp_wifi_connect() : ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_svc_disconnect(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    LOCK();
    s_want_connected = false;
    s_st.state = WIFI_SVC_IDLE;
    UNLOCK();
    return esp_wifi_disconnect();
}

esp_err_t wifi_svc_forget(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    wifi_svc_disconnect();
    wifi_config_t cfg = { 0 };
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    LOCK();
    refresh_saved();
    s_st.ssid[0] = '\0';
    UNLOCK();
    return err;
}

void wifi_svc_get_status(wifi_svc_status_t *out)
{
    if (s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    wifi_ap_record_t ap;
    bool live = false;
    LOCK();
    live = s_st.state == WIFI_SVC_CONNECTED;
    UNLOCK();
    if (live && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        LOCK();
        s_st.rssi = ap.rssi;
        UNLOCK();
    }
    LOCK();
    *out = s_st;
    UNLOCK();
}

int wifi_svc_get_scan(wifi_svc_ap_t *out, int max)
{
    if (s_lock == NULL) {
        return 0;
    }
    LOCK();
    int n = s_nap < max ? s_nap : max;
    memcpy(out, s_aps, n * sizeof(*out));
    UNLOCK();
    /* strongest first */
    for (int i = 1; i < n; i++) {
        wifi_svc_ap_t t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    return n;
}
