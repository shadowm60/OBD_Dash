# wifi_svc

Wi-Fi station: scan, connect, reconnect, and SNTP time.

## What it does

Runs the ESP32 as a Wi-Fi station. The driver stores the last network
(SSID + password) in NVS itself; this component adds scanning, a background
reconnect loop, and SNTP time sync. After an IP is obtained the clock is set
over SNTP (`CONFIG_APP_NTP_SERVER`, zone `CONFIG_APP_TZ`); a callback lets the
app copy that time into the RTC.

All calls are non-blocking and thread safe; poll `wifi_svc_get_status()` /
`wifi_svc_get_scan()`.

## Dependencies

`esp_wifi`, `esp_netif`, `esp_event`, `esp_timer`, `lwip` (private).

## Integration

Set the time-sync callback **before** init, then start (optionally
reconnecting to the stored network at boot):

```c
wifi_svc_set_sync_cb(store_time_in_rtc);
wifi_svc_init(settings.wifi_autoconnect);    /* true: connect now + keep retrying */
```

`web_svc_init()` must come after this (it needs `esp_netif`).

## API

```c
wifi_svc_scan();
wifi_svc_ap_t aps[WIFI_SVC_MAX_APS];
int n = wifi_svc_get_scan(aps, WIFI_SVC_MAX_APS);   /* strongest first */

wifi_svc_connect("ssid", "password");   /* "" password = open; stored in NVS */
wifi_svc_connect_saved();
wifi_svc_disconnect();
wifi_svc_forget();                      /* disconnect + erase stored network */

wifi_svc_set_auto_retry(true);          /* the "Connect at boot" setting, live */

wifi_svc_status_t st; wifi_svc_get_status(&st);
/* st.state, st.ip, st.rssi, st.time_synced, st.retry_waiting, st.last_reason_text, ... */
```

## Gotchas

- Autoconnect retries every 30 s while the network is out of range and after a
  lost connection. A **manual** `wifi_svc_disconnect()` or `wifi_svc_forget()`
  stops the retries; `wifi_svc_set_auto_retry()` toggles them live.
- The time-sync callback runs in the SNTP task — keep it short (it just writes
  the RTC in this project).
- `retry_waiting` in the status means FAILED but a background attempt is
  queued, as opposed to a hard IDLE.
