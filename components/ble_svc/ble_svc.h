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
 * Serial port (BLE ELM327 OBD adapters): after discovery the service looks
 * for a write + notify pair - Vgate's bef8d6c9-... characteristic first, then
 * the 0x18F0 layout (write 0x2AF1, notify 0x2AF0), then HM-10 style 0xFFE1 -
 * switches notifications on and buffers what arrives. ble_svc_serial_*().
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
#define BLE_SVC_MAX_SVCS    32      /* lines: services + their characteristics */

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
    char     svcs[BLE_SVC_MAX_SVCS][40];    /* "180F Battery", then "  2A19 R N" per characteristic
                                              (R read, W write, w write without response,
                                              N notify, I indicate); full UUIDs in the log */
    bool     svc_done;          /* service + characteristic discovery finished */
    int      nbonds;
    char     last_event[48];    /* human readable, e.g. "pairing failed (13)" */
    bool     serial_ready;      /* serial port found, notifications on */
    char     serial_info[40];   /* "bef8d6c9 (Vgate), MTU 247", "" = none found */
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

/* Serial port of the connected device (see the top). write: split into
 * MTU-sized packets; read: what arrived since the last call, waits up to
 * `wait_ms` for the first byte (0 = don't wait). */
bool      ble_svc_serial_ready(void);
esp_err_t ble_svc_serial_write(const void *data, size_t len);
size_t    ble_svc_serial_read(void *buf, size_t max, uint32_t wait_ms);
void      ble_svc_serial_flush(void);

/* Copies up to `max` devices of the current / last scan, in discovery order
 * (the index is what ble_svc_connect() takes). */
int ble_svc_get_devices(ble_svc_dev_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* BLE_SVC_H */
