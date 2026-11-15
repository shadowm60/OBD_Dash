# ble_svc

Bluetooth LE central (NimBLE): scan, connect, pair, and a serial port for
BLE ELM327 OBD adapters.

## What it does

Acts as a BLE central: scans, connects, pairs ("just works" / numeric
comparison, bonding kept in NVS), and lists the GATT services of the connected
device. The ESP32-S3 is **BLE only** (no classic BR/EDR), so classic-only
devices (old headsets) do not appear.

Serial port: after discovery it looks for a write+notify characteristic pair —
Vgate's `bef8d6c9-…` first, then the `0x18F0` layout (write `0x2AF1`, notify
`0x2AF0`), then HM-10 style `0xFFE1` — enables notifications and buffers
incoming bytes. This is what [../obd](../obd)'s ELM327 backend talks through.

All calls are non-blocking and thread safe; poll `ble_svc_get_status()`.

## Dependencies

`bt` (NimBLE), `esp_timer` (private).

## Integration

```c
ble_svc_init();
ble_svc_scan(5);                      /* active scan, seconds */
ble_svc_connect(index);               /* index from the last scan; starts pairing */
```

Use it as the ELM327 adapter's serial I/O by wrapping the serial functions in
an `obd_elm_io_t` and passing it to `obd_client_set_elm_io()`:

```c
static const obd_elm_io_t io = {
    .ready = ble_svc_serial_ready,
    .write = (void*)ble_svc_serial_write,
    .read  = ble_svc_serial_read,
};
```

Autoconnect (remembered adapter, reconnects whenever nothing is connected):

```c
ble_svc_set_autoconnect(true, addr, addr_type, "IOS-Vlink");
```

## API

| Function | Purpose |
|---|---|
| `ble_svc_scan(s)` / `ble_svc_scan_stop()` | scan control |
| `ble_svc_get_devices(out, max)` | last scan results, discovery order |
| `ble_svc_connect(i)` / `ble_svc_disconnect()` | connect / drop |
| `ble_svc_forget_all()` | delete all bonds |
| `ble_svc_set_autoconnect(on, addr, type, name)` | auto-reconnect to one device |
| `ble_svc_serial_ready/write/read/flush` | serial port of the connected device |
| `ble_svc_get_status(&st)` | state, peer, services, `serial_ready`, `serial_info` |

Status includes the discovered service/characteristic list (`svcs[]`, with
`R/W/w/N/I` flags), pairing state, and the numeric-comparison passkey (shown,
auto-confirmed).

## Gotchas

- A device that requires a **typed passkey** is not supported yet (only "just
  works" and numeric comparison).
- `ble_svc_disconnect()` pauses autoconnect until the next manual
  `ble_svc_connect()`.
- `ble_svc_serial_read()` returns what arrived since the last call and waits up
  to `wait_ms` for the first byte (0 = don't wait); writes are split into
  MTU-sized packets.
- If a console (PC tool) has talked to the adapter, call
  `obd_client_elm_invalidate()` so the ELM327 is set up again.
