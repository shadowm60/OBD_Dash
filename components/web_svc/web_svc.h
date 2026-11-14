/*
 * web_svc - log file manager over Wi-Fi (HTTP, port 80).
 *
 * Open http://<dash IP>/ (shown on the Wi-Fi page) in a browser on the same
 * network: the log files on the SD card (can_, evt_, par_, dtc_), grouped by
 * type, newest first, with size; download, delete one or several. Files that
 * are being written right now can be downloaded (contents so far) but not
 * deleted.
 *
 *   GET  /                 the page (one HTML file, no external assets)
 *   GET  /api/list         JSON: card size / free, files {n, s, open}
 *   GET  /dl/<file>        download
 *   POST /del/<file>       delete
 *
 * Only plain file names in the card's root with one of the log prefixes (no
 * '/', no ".."). No password: anyone on the same network can use it. The
 * server task's stack is in PSRAM; files are streamed in 8 KB blocks.
 */
#ifndef WEB_SVC_H
#define WEB_SVC_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the server (needs esp_netif, i.e. after wifi_svc_init()). */
esp_err_t web_svc_init(void);

bool web_svc_running(void);

#ifdef __cplusplus
}
#endif

#endif /* WEB_SVC_H */
