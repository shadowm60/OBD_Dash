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
                  live_view.c   live screens: gauge scaling, cached gauge backgrounds, swipe navigation
                  live_values.c signals -> v_* text variables of the live screens ("--" when stale)
comsm           connection state machine task, owns the OBD client
                comsm_diag: DTC read/clear + freeze frame jobs (pause polling while running)
obd             obd_client (one request in flight, timeout, NRC, addressing)
                obd_pids   (Service 01 catalogue in flash, support/enable state, scheduler)
isotp           ISO 15765-2 transport (isotp-c) + glue to can_if
can_if          esp_twai driver: TX queue, RX dispatch by ID range, bus stats
datalog         SD logging task: raw CAN (candump .log) + polled parameters (.csv)
vehicle_data    latest value + timestamp of every signal (producers -> consumers)
settings        persistent user config in NVS (one versioned blob, saved 2 s after last change)
board           LCD, touch, IO expander, SD card, LVGL port (no app knowledge)
```

### Common changes

| Want to...                         | Touch                                                        |
|------------------------------------|--------------------------------------------------------------|
| add a Service 01 PID               | `obd/obd_pid_table.c` (+ `vehicle_data/vehicle_signals.def`) |
| show a value on a live screen      | EEZ Studio: string variable + tile (copy one), `tools/eez-build.sh`, row in `s_values[]` (`hmi/bridge/live_values.c`) |
| drive a gauge needle               | EEZ variable on the meter, row in `s_bindings[]` (`hmi/bridge/ui_bridge.c`), scale fix in `live_view.c` |
| regenerate the UI code             | `tools/eez-build.sh` (headless EEZ Studio build, same as Build in the GUI) |
| handle a new EEZ action            | `hmi/bridge/actions.c`                                       |
| change bitrate / timeouts          | `idf.py menuconfig` -> BSW Configuration -> CAN / OBD        |
| persist a new setting              | field in `settings.h` + setter, bump `SETTINGS_VER`, sync in `settings_view.c` |
| add a diagnostic job               | new steps in `comsm/comsm_diag.c`, command in `comsm.c`      |
| log row rate / full bus capture    | `idf.py menuconfig` -> BSW Configuration -> Data logging / CAN |

### Connection sequence

1. `Connect` -> `01 00` functional on `0x7DF`; first responder in `0x7E8..0x7EF` is used.
2. Switch to physical addressing (`0x7E0` -> `0x7E8`), read `01 20`, `01 40`, ... while supported.
3. Poll the PIDs selected in Settings > Select Params (only ECU-supported ones can be
   selected): fast group continuously, medium every 500 ms, slow every 2 s. With nothing
   selected the bus stays quiet (optional keepalive: `CONFIG_OBD_KEEPALIVE_MS`).
4. `CONFIG_OBD_MAX_CONSECUTIVE_TIMEOUTS` timeouts in a row -> reconnect after `CONFIG_OBD_RECONNECT_DELAY_MS`.

CAN: 11-bit IDs, `CONFIG_CAN_BITRATE` (default 500 kbit/s), hardware filter `0x7E8/0x7F8`,
ISO-TP frames padded to 8 bytes, flow control sent to the physical request ID.

## Live data screens

Start screen buttons: **Driving View**, **Race View**, **Tuning View**, **Read DTCs**,
Settings, System. On a live screen, swipe left/right to cycle Drive -> Race -> Tune; swipe
up/down or long press to return to the start screen. A tile shows `--` when its PID is not
selected in Settings > Select Params, not supported, or no reply for 10 s.

Every field is fed by one Mode 01 PID (or two for derived values). Select the PIDs you
want in Settings > Select Params; only selected and supported PIDs are polled.

### Drive

Everyday driving: big RPM gauge on the left, tiles on the right.

| Field | Unit | PID | Notes |
|---|---|---|---|
| RPM gauge (needle) | rpm x1000 | 0C | scale 0..8, red zone from about 6.5 |
| RPM (digital, under the gauge) | rpm | 0C | |
| SPEED | km/h | 0D | large tile |
| COOLANT | °C | 05 | |
| FUEL | % | 2F | fuel tank level |
| BATTERY | V | 42 | control module voltage |
| INTAKE | °C | 0F | intake air temperature |
| CHECK ENGINE | – | 01 | see MIL below |

### Race

The original two-gauge screen (`liveData` in EEZ) with three tiles on each side.

| Field | Unit | PID | Notes |
|---|---|---|---|
| RPM gauge (needle) | rpm x100 | 0C | scale 0..90 |
| AFR gauge (needle) | AFR | 24 or 34 | lambda x 14.7, scale 10..20 |
| SPEED | km/h | 0D | left column |
| BOOST | bar | 0B (+ 33) | left column, MAP - BARO, see below |
| INTAKE | °C | 0F | left column |
| AFR | AFR | 24 or 34 | right column, lambda x 14.7 |
| COOLANT | °C | 05 | right column |
| OIL | °C | 5C | right column, engine oil temperature |
| CHECK ENGINE | – | 01 | see MIL below |

### Tune

No gauges, a 4 x 4 grid of text tiles. All of it is also in `par_NNN.csv` when parameter
logging is on.

| Row | Field | Unit | PID |
|---|---|---|---|
| 1 | RPM | rpm | 0C |
| 1 | LOAD | % | 04 (calculated engine load) |
| 1 | MAP | kPa | 0B |
| 1 | BOOST | bar | 0B (+ 33) |
| 2 | THROTTLE | % | 11 |
| 2 | TIMING | ° before TDC | 0E |
| 2 | MAF | g/s | 10 |
| 2 | INTAKE | °C | 0F |
| 3 | STFT B1 / LTFT B1 | % | 06 / 07 |
| 3 | STFT B2 / LTFT B2 | % | 08 / 09 |
| 4 | AFR | AFR | 24 or 34 (lambda x 14.7) |
| 4 | AFR TARGET | AFR | 44 (commanded lambda x 14.7) |
| 4 | COOLANT | °C | 05 |
| 4 | FUEL | L/h | 5E (engine fuel rate) |
| – | CHECK ENGINE | – | 01 |

### Shared rules

- **MIL:** `CHECK ENGINE` appears top right on all three screens when PID 01 reports the
  MIL on; `(n)` is the stored DTC count from the same PID.
- **Boost** = (MAP - BARO) / 100 bar, from PID 0B minus PID 33. If 33 is not polled,
  101.3 kPa is used.
- **AFR** assumes gasoline (stoichiometric 14.7). PID 24 (O2 sensor 1, wide range,
  voltage) and PID 34 (same, current) both feed it; select whichever your ECU supports.
- **Needle animation** (System > Gauge animation, on by default): each new sample moves
  the needle linearly to the new value over about the time between samples, so it keeps
  moving instead of jumping. The needle lags by up to one sample interval; untick it to
  show each sample as it arrives. Text tiles are never animated.
- **Read speed** (System > Read speed, x1..x6, default x6): how many PIDs are asked for in
  one Service 01 request (ISO 15765-4 allows 6). The ECU's answer time, not the bus or
  the CPU, limits the rate, so asking for 6 at once gives up to 6x the values per second.
  If the ECU rejects or half-answers a multi-PID request, the dash drops to 1 PID per
  request by itself (the System screen then says "ECU accepts 1 PID per request");
  moving the slider tries again. The status line shows the result as `values/s`.
  Due slow/medium PIDs (temperatures, trims) get at most half of each request; the
  rest goes to the fast ones (rpm, speed, load, ...).
- **Brightness** (System > slider, 10..100 %). The board's backlight driver (AP3032) is
  only switched on/off by the CH422G (EXIO2), so by default dimming is done in software:
  a black semi-transparent layer over the UI (the backlight keeps full power).
  Real dimming needs a wire: connect a GPIO whose function you don't use to the dimming
  pad next to R11 (10K) / C11 (1uF) in the backlight section of the schematic, and set
  `CONFIG_BACKLIGHT_PWM_GPIO` (menuconfig > Display brightness). The firmware then drives
  an inverted 20 kHz PWM there. Every ESP32 pin is already assigned on this board
  (GPIO6 = RTC interrupt, GPIO43/44 = RS485, ...). Auto brightness is disabled: the board
  has no light sensor.
- The formatting (decimals, derived values) is in `components/hmi/bridge/live_values.c`;
  the gauge scaling is in `components/hmi/bridge/live_view.c`.

The layout lives in `components/hmi/OBD_Dash.eez-project` (EEZ Studio 0.29). On screens
with a meter, the scale, arcs and labels lying fully inside a meter are drawn once into a
cached image; values placed on a meter must sit inside a panel, like the tiles do.

## Logging

Switched with the two checkboxes on the Settings screen (state is saved). Files go to the
SD card root, numbered so nothing is overwritten. Times are seconds since boot (no RTC).

| Checkbox | File | Content |
|---|---|---|
| Log raw CAN | `can_NNN.log` | every frame received and sent, candump format (`(1.234000) can0 7E8#04410C1AF8`), opens in SavvyCAN / python-can / `canplayer`. Only `0x7E8..0x7EF` + our requests, or every frame on the bus with the **All traffic** checkbox next to it (the hardware filter is only opened while a raw log is written; a busy car bus can be 1000-2000 frames/s, ~60-80 KB/s on the card) |
| (with Log raw CAN) | `evt_NNN.txt` | what the dash itself did, same time base as the raw log: connection state changes, request timeouts, and once a second (only when something changed) the CAN error counters: failed sends, send retries, dropped frames, bus errors, bus state. A request that failed to go out never appears in `can_NNN.log`; it shows up here |
| Log parameters | `par_NNN.csv` | one row per `CONFIG_DATALOG_PARAM_PERIOD_MS` (200 ms), one column per polled PID, empty cell = no fresh value. A new file starts on every connect or PID selection change |

Files are synced every `CONFIG_DATALOG_SYNC_PERIOD_MS` (2 s); untick the box before
pulling the card. Status line shows `REC` while a log is open.

## Roadmap

1. Communication layer (done: can_if, obd, comsm, vehicle_data, ui_bridge)
2. Settings in NVS, DTC read/clear (Service 03/07/0A/04), freeze frame (Service 02) (done)
3. Live data screens: Drive / Race / Tune (done)
4. SD card logging (done)
