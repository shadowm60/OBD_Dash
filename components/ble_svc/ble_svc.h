/*
 * ble_svc - Bluetooth LE central on NimBLE: scan, connect, pair, list the
 * GATT services of the connected device.
 *
 * The ESP32-S3 has Bluetooth LE only (no BR/EDR "classic"): phones, sensors,
 * BLE keyboards etc. show up, classic-only devices (old headsets) do not.
 *
 * Pairing: "just works" or numeric comparison (auto-confirmed, the number is
 * shown in the status), bonding on; bonds are kept in NVS by NimBLE. A device
 * that requires a passkey to be typed in is not supported yet.
 *
 * All calls are non-blocking and thread safe; poll ble_svc_get_status().
 */
#ifndef BLE_SVC_H
#define BLE_SVC_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SVC_MAX_DEVS    40
#define BLE_SVC_MAX_SVCS    16

typedef enum {
    BLE_SVC_OFF = 0,            /* host not synced yet */
    BLE_SVC_READY,
    BLE_SVC_SCANNING,
    BLE_SVC_CONNECTING,
    BLE_SVC_CONNECTED,
} ble_svc_state_t;

typedef struct {
    uint8_t addr[6];            /* little endian, as NimBLE */
    uint8_t addr_type;
    char    addr_str[18];
    char    name[32];           /* "" if not advertised */
    int8_t  rssi;
    bool    connectable;
} ble_svc_dev_t;

typedef struct {
    ble_svc_state_t state;
    uint32_t scan_count;        /* increments with every finished scan */
    int      ndev;
    /* connected peer */
    char     peer_addr[18];
    char     peer_name[32];
    int8_t   peer_rssi;
    bool     encrypted;
    bool     bonded;
    bool     authenticated;
    uint32_t passkey;           /* numeric comparison value, 0 = none */
    int      nsvc;
    char     svcs[BLE_SVC_MAX_SVCS][40];    /* "180F Battery", 128-bit UUIDs shortened */
    bool     svc_done;          /* service discovery finished */
    int      nbonds;
    char     last_event[48];    /* human readable, e.g. "pairing failed (13)" */
} ble_svc_status_t;

esp_err_t ble_svc_init(void);

/* Scans for `seconds` (active scan, names from scan responses). */
esp_err_t ble_svc_scan(int seconds);
esp_err_t ble_svc_scan_stop(void);

/* Connect to entry `index` of the last scan and start pairing. */
esp_err_t ble_svc_connect(int index);
esp_err_t ble_svc_disconnect(void);

/* Delete all bonds. */
esp_err_t ble_svc_forget_all(void);

void ble_svc_get_status(ble_svc_status_t *out);

/* Copies up to `max` devices of the current / last scan, in discovery order
 * (the index is what ble_svc_connect() takes). */
int ble_svc_get_devices(ble_svc_dev_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* BLE_SVC_H */
