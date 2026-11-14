/*
 * wifi_svc - Wi-Fi station: scan, connect, reconnect, SNTP time.
 *
 * All calls are non-blocking and thread safe; results are polled with
 * wifi_svc_get_status() / wifi_svc_get_scan(). The driver stores the last
 * network (SSID + password) in NVS itself; wifi_svc_init(true) reconnects to
 * it at boot. After an IP is obtained the clock is set over SNTP
 * (CONFIG_APP_NTP_SERVER, time zone CONFIG_APP_TZ).
 */
#ifndef WIFI_SVC_H
#define WIFI_SVC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SVC_MAX_APS    24

typedef enum {
    WIFI_SVC_IDLE = 0,          /* not connected, not trying */
    WIFI_SVC_CONNECTING,
    WIFI_SVC_CONNECTED,         /* associated and got an IP */
    WIFI_SVC_FAILED,            /* gave up, see last_reason */
} wifi_svc_state_t;

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    uint8_t channel;
    bool    secure;             /* needs a password */
    char    auth[12];           /* "open", "WPA2", "WPA2/3", ... */
} wifi_svc_ap_t;

typedef struct {
    wifi_svc_state_t state;
    char     ssid[33];          /* network connected / being connected to */
    bool     has_saved;         /* a network is stored in NVS */
    char     saved_ssid[33];
    char     ip[16];
    char     gw[16];
    char     netmask[16];
    char     dns[16];
    int8_t   rssi;              /* live, while connected */
    uint8_t  channel;
    char     mac[18];
    uint8_t  last_reason;       /* last disconnect reason code */
    char     last_reason_text[28];
    uint32_t connected_since_s; /* uptime seconds at connect */
    bool     time_synced;       /* SNTP done at least once */
    bool     scanning;
    uint32_t scan_count;        /* increments with every finished scan */
    bool     retry_waiting;     /* FAILED, next attempt in the background (auto retry) */
} wifi_svc_status_t;

/* Called (in the SNTP task) after every successful time sync, e.g. to store
 * the time in the RTC. Set before wifi_svc_init(). */
typedef void (*wifi_svc_sync_cb_t)(void);
void wifi_svc_set_sync_cb(wifi_svc_sync_cb_t cb);

/* Starts the station. autoconnect: connect to the stored network now, and
 * keep trying (every 30 s) while it is out of range, also after a lost
 * connection. A manual disconnect / forget stops the retries. */
esp_err_t wifi_svc_init(bool autoconnect);

/* Background retries on / off (the "Connect at boot" setting), live. */
void wifi_svc_set_auto_retry(bool on);

esp_err_t wifi_svc_scan(void);

/* Connect to `ssid` (password "" = open network). Stored in NVS by the driver. */
esp_err_t wifi_svc_connect(const char *ssid, const char *password);

/* Reconnect to the network stored in NVS. */
esp_err_t wifi_svc_connect_saved(void);

esp_err_t wifi_svc_disconnect(void);

/* Disconnect and erase the stored network. */
esp_err_t wifi_svc_forget(void);

void wifi_svc_get_status(wifi_svc_status_t *out);

/* Copies up to `max` scan results (strongest first), returns the count. */
int wifi_svc_get_scan(wifi_svc_ap_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_SVC_H */
