# OBD_Dash
ESP32-S3 based OBD-II dashboard with logging capabilities.

Based on Waveshare ESP32-S3-Touch-LCD-5
    > https://www.waveshare.com/esp32-s3-touch-lcd-5.htm
    > https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-5

## Architecture

Dependencies only point downwards. Only the LVGL task touches LVGL; the bus
side never blocks the UI.

```
main            init order / wiring only
hmi             EEZ Studio UI (src/ui, generated) + bridge/ (hand written)
  bridge/         ui_bridge.c  lv_timer: comsm + vehicle_data -> EEZ variables
                  actions.c    EEZ actions -> comsm / obd calls
                  eez_vars.cpp typed EEZ variable setters (strings are copied)
                  settings_view.c / diag_view.c / pid_select_view.c  per-screen controllers
comsm           connection state machine task, owns the OBD client
                comsm_diag: DTC read/clear + freeze frame jobs (pause polling while running)
obd             obd_client (one request in flight, timeout, NRC, addressing)
                obd_pids   (Service 01 catalogue in flash, support/enable state, scheduler)
isotp           ISO 15765-2 transport (isotp-c) + glue to can_if
can_if          esp_twai driver: TX queue, RX dispatch by ID range, bus stats
vehicle_data    latest value + timestamp of every signal (producers -> consumers)
settings        persistent user config in NVS (one versioned blob, saved 2 s after last change)
board           LCD, touch, IO expander, SD card, LVGL port (no app knowledge)
```

### Common changes

| Want to...                         | Touch                                                        |
|------------------------------------|--------------------------------------------------------------|
| add a Service 01 PID               | `obd/obd_pid_table.c` (+ `vehicle_data/vehicle_signals.def`) |
| show a signal in an EEZ variable   | regenerate EEZ code, add a row to `s_bindings[]` in `hmi/bridge/ui_bridge.c` |
| handle a new EEZ action            | `hmi/bridge/actions.c`                                       |
| change bitrate / timeouts          | `idf.py menuconfig` -> BSW Configuration -> CAN / OBD        |
| persist a new setting              | field in `settings.h` + setter, bump `SETTINGS_VER`, sync in `settings_view.c` |
| add a diagnostic job               | new steps in `comsm/comsm_diag.c`, command in `comsm.c`      |

### Connection sequence

1. `Connect` -> `01 00` functional on `0x7DF`; first responder in `0x7E8..0x7EF` is used.
2. Switch to physical addressing (`0x7E0` -> `0x7E8`), read `01 20`, `01 40`, ... while supported.
3. Poll the PIDs selected in Settings > Select Params (only ECU-supported ones can be
   selected): fast group continuously, medium every 500 ms, slow every 2 s. With nothing
   due, `01 00` is sent once a second as a keepalive.
4. `CONFIG_OBD_MAX_CONSECUTIVE_TIMEOUTS` timeouts in a row -> reconnect after `CONFIG_OBD_RECONNECT_DELAY_MS`.

CAN: 11-bit IDs, `CONFIG_CAN_BITRATE` (default 500 kbit/s), hardware filter `0x7E8/0x7F8`,
ISO-TP frames padded to 8 bytes, flow control sent to the physical request ID.

## Roadmap

1. Communication layer (done: can_if, obd, comsm, vehicle_data, ui_bridge)
2. Settings in NVS, DTC read/clear (Service 03/07/0A/04), freeze frame (Service 02) (done)
3. Live data screens (EEZ Studio)
4. SD card logging
