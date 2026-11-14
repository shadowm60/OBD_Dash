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
  bridge/         hmi.c        ui_init, bridge timers, EEZ tick only when a variable changed
                  uivar.c / nav.c  menu pages: EEZ variables and screens by name (from Esp32_monitor)
                  view_sys.c   status line, Home, Display;  view_obd.c  OBD + CAN page options, logging
                  view_wifi.c / view_ble.c  Wi-Fi and BLE pages;  screen.c  keyboard, screen timeout
                  ui_bridge.c  lv_timer: comsm + vehicle_data -> EEZ variables (connection, needles)
                  actions.c    EEZ actions -> comsm / obd calls
                  eez_vars.cpp typed EEZ variable setters (strings are copied)
                  diag_view.c / pid_select_view.c  trouble codes, freeze frame, parameter list
                  live_view.c   live screens: gauge scaling, cached gauge backgrounds, swipe navigation
                  live_values.c signals -> v_* text variables of the live screens ("--" when stale)
comsm           connection state machine task, owns the OBD client
                comsm_diag: DTC read/clear + freeze frame jobs (pause polling while running)
obd             obd_client (one request in flight, timeout, NRC, addressing)
                obd_pids   (Service 01 catalogue in flash, support/enable state, scheduler)
isotp           ISO 15765-2 transport (isotp-c) + glue to can_if
can_if          esp_twai driver: start / restart (bitrate, listen-only), TX queue with retries,
                RX dispatch by ID range, two monitor taps (log, bus monitor), bus stats
datalog         SD logging task: raw CAN (candump .log) + polled parameters (.csv)
io_svc          IO page: DI / DO through the CH422G, I2C scan, chip temperature
canmon          CAN page bus monitor: ID table, rates, bus load (from Esp32_monitor, without its logger)
wifi_svc        Wi-Fi station: scan, connect, stored network, retry in the background, SNTP clock (from Esp32_monitor)
web_svc         log file manager over Wi-Fi (http://<IP>/): download / delete the SD card logs
dash_svc        Dash page: decodes a CAN broadcast (rusEFI) with a DBC from the SD card
dbc             DBC parser + frame decoder, pure C (PC test: tools/test/test_dbc.sh)
ble_svc         Bluetooth LE central (NimBLE): scan, connect, pair (from Esp32_monitor)
vehicle_data    latest value + timestamp of every signal (producers -> consumers)
settings        persistent user config in NVS (one versioned blob, saved 2 s after last change)
board           LCD, touch, IO expander, SD card, LVGL port, PCF85063 RTC (no app knowledge)
```

### Common changes

| Want to...                         | Touch                                                        |
|------------------------------------|--------------------------------------------------------------|
| add a Service 01 PID               | `obd/obd_pid_table.c` (+ `vehicle_data/vehicle_signals.def`) |
| show a value on a live screen      | EEZ Studio: string variable + tile (copy one), `tools/eez-build.sh`, row in `s_values[]` (`hmi/bridge/live_values.c`) |
| drive a gauge needle               | EEZ variable on the meter, row in `s_bindings[]` (`hmi/bridge/ui_bridge.c`), scale fix in `live_view.c` |
| change a menu page (sidebar pages)  | edit its builder in `tools/obd_ui.py`, run it, then `tools/eez-build.sh`. EEZ Studio edits to these pages are overwritten by the next run; the live views are only edited in EEZ Studio |
| regenerate the UI code             | `tools/eez-build.sh` (headless EEZ Studio build, same as Build in the GUI) |
| handle a new EEZ action            | `hmi/bridge/actions.c`                                       |
| change bitrate / timeouts          | `idf.py menuconfig` -> BSW Configuration -> CAN / OBD        |
| persist a new setting              | field at the END of `app_settings_t` (no version bump), read / write it in the page's view (`settings_get` / `settings_put`) |
| add a diagnostic job               | new steps in `comsm/comsm_diag.c`, command in `comsm.c`      |
| log row rate / full bus capture    | `idf.py menuconfig` -> BSW Configuration -> Data logging / CAN |

### Connection sequence

1. `Connect` -> `01 00` functional on `0x7DF`; first responder in `0x7E8..0x7EF` is used.
2. Switch to physical addressing (`0x7E0` -> `0x7E8`), read `01 20`, `01 40`, ... while supported.
3. Poll the PIDs selected in OBD > Select parameters (only ECU-supported ones can be
   selected): fast group continuously, medium every 500 ms, slow every 2 s, up to 6 PIDs
   per request (OBD > Read speed). With nothing
   selected the bus stays quiet (optional keepalive: `CONFIG_OBD_KEEPALIVE_MS`).
4. `CONFIG_OBD_MAX_CONSECUTIVE_TIMEOUTS` timeouts in a row -> reconnect after `CONFIG_OBD_RECONNECT_DELAY_MS`.

CAN: 11-bit IDs, `CONFIG_CAN_BITRATE` (default 500 kbit/s), hardware filter `0x7E8/0x7F8`
(opened for a full bus capture with CAN > All traffic), ISO-TP frames padded to 8
bytes, flow control sent to the physical request ID. A send that loses arbitration to the
car's traffic is retried (up to 16 attempts within 20 ms): the ESP32-S3's CAN controller
would otherwise try each frame only once.

## Pages

The look comes from Esp32_monitor (`~/git/Esp32_monitor`): dark theme, a sidebar with
the page buttons and a status line (OBD, REC, Wi-Fi, BLE, SD card, clock) on every page.

| Page | What it does |
|---|---|
| **Home** | Board and firmware, uptime, clock, free RAM; one line per service (OBD, CAN, Wi-Fi, BLE, SD, logging) |
| **OBD** | Connect / disconnect, ECU status, MIL telltale next to Connect while connected (red = MIL on, amber = stored codes without MIL, green = none; tap = DTC page), the three live views, Select parameters, read speed, log parameters, Link: board CAN or a BLE ELM327 adapter (connect it on the BLE page first; used from the next Connect) |
| **DTC** | Read / clear / save the trouble codes, freeze frame. Read (and Clear, freeze frame) connect to the ECU first when OBD is not connected; it stays connected afterwards |
| **Dash** | 3 x 3 tiles with signals decoded from a CAN broadcast (rusEFI first) with a DBC file in `/db/dbc/` on the SD card (upload it on the web page); `--` when a signal stops. **Settings**: DBC file, base ID (bus ID of the DBC's first message; rusEFI's CAN base is configurable, default 512 = 0x200; all 11-bit IDs move with it), which signal each tile shows (saved in `/db/dash/<dbc>.txt`, editable by hand), All signals (table). Listen only, board CAN, CAN page bitrate |
| **Diag** | placeholder: UDS diagnostics (VW group, Mercedes), see CLAUDE.md roadmap |
| **CAN** | Bus monitor: every ID on the bus (DLC, last data, count, period; our own OBD requests too), frames/s, peak, bus load, error counters. Bitrate and listen-only (only while OBD is disconnected; Connect switches listen-only off). Raw log options |
| **Wi-Fi** | Scan, connect, forget, auto connect; IP, RSSI, SNTP time (stored in the RTC), address of the log file web page |
| **BLE** | 10 s scan, connect + pair, GATT services, forget bonds; Adapter console: raw commands to a BLE ELM327 adapter (ATZ, ATI, ATRV, 0100, custom) |
| **IO** | Isolated inputs DI0 / DI1 (state, change counters; read only while the page is open), outputs DO0 / DO1 (tap to switch; off at power-up), I2C scan of the terminal's SDA / SCL, chip temperature |
| **Display** | Brightness, screen timeout, gauge animation, SD card (remount), restart |

Text fields open an on-screen keyboard.

### Inputs and outputs (terminal block)

- **DI0 / DI1:** optocoupler inputs, 5-36 V between DIN0/DIN1 and DI COM, either polarity
  (NPN / PNP sensors, dry contacts with DI COM to the supply). Active = current flows.
- **DO0 / DO1:** optocoupler-isolated open-drain outputs to GND, 5-36 V, max 450 mA each.
  Off at power-up, never stored.
- The inputs sit on the CH422G IO expander, which has a single direction bit for all eight
  IO pins: reading them means switching every pin to input for ~0.3 ms, the SD card's CS
  included. The read holds the SD card's SPI bus meanwhile (between two SD commands), and
  only runs while the IO page is open.

## Live data screens

The **OBD** page opens them: **Drive**, **Race**, **Tune**. On a live screen, swipe
left/right to cycle Drive -> Race -> Tune; swipe up/down or long press to return to the OBD
page. The last page (live view or menu page) is shown again at power-up. A tile shows `--` when its PID is not
selected in OBD > Select parameters, not supported, or no reply for 10 s.

Every field is fed by one Mode 01 PID (or two for derived values). Select the PIDs you
want in OBD > Select parameters; only selected and supported PIDs are polled.

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
| max (under the RPM gauge) | rpm | 0C | peak hold |
| max (BOOST tile) | bar | 0B (+ 33) | peak hold |
| min - max (AFR tile) | AFR | 24 or 34 (+ 11) | peak hold, only while throttle >= 50 % (any throttle if PID 11 is not polled), so idle and overrun fuel cut don't count |

**Peak hold:** kept since boot or the last reset, also over a disconnect. Tap the RPM max,
the BOOST tile or the AFR tile to reset all three (a swipe starting there still changes
screens; a long press still returns to the OBD page). Every new sample counts, at the
rate the PID is polled.

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
  MIL on; `(n)` is the stored DTC count from the same PID. PID 01 is always polled when the ECU supports
  it (shown checked and locked in Select parameters), so the MIL works without selecting
  it; it costs one request every ~2 s.
- **Boost** = (MAP - BARO) / 100 bar, from PID 0B minus PID 33. If 33 is not polled,
  101.3 kPa is used.
- **AFR** assumes gasoline (stoichiometric 14.7). PID 24 (O2 sensor 1, wide range,
  voltage) and PID 34 (same, current) both feed it; select whichever your ECU supports.
- **Needle animation** (Display > Gauge animation, on by default): each new sample moves
  the needle linearly to the new value over about the time between samples, so it keeps
  moving instead of jumping. The needle lags by up to one sample interval; untick it to
  show each sample as it arrives. Text tiles are never animated.
- **Read speed** (OBD > Read speed slider, x1..x6, default x6): how many PIDs are asked for in
  one Service 01 request (ISO 15765-4 allows 6). The ECU's answer time, not the bus or
  the CPU, limits the rate, so asking for 6 at once gives up to 6x the values per second.
  If the ECU rejects or half-answers a multi-PID request, the dash drops to 1 PID per
  request by itself (the OBD page then says "ECU accepts 1 PID");
  moving the slider tries again. The status line shows the result as `values/s`.
  Due slow/medium PIDs (temperatures, trims) get at most half of each request; the
  rest goes to the fast ones (rpm, speed, load, ...).
- **Brightness** (Display > slider, 10..100 %). The board's backlight driver (AP3032) is
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

Switched on the CAN page (Log raw CAN, All traffic) and the OBD page (Log parameters);
the choice is saved. A ticked box arms the log; files are only written during a session:
the raw log while OBD is connected or the CAN page monitor runs (Start monitor), the
parameter log while OBD is connected. Disconnect / Stop monitor closes the file, the next
session starts a new one; a short ECU dropout (reconnect) keeps the file. Files go to the
SD card root. Once the clock is set they are named by date and time
(`can_20261114_095436.log`) and stamped in Unix time; the CSV also gets a `local_time`
column. Before that (clock never set) they are numbered (`can_003.log`) and stamped in
seconds since boot. The clock comes from Wi-Fi (SNTP) and is kept in the board's RTC
(PCF85063, backup cell), so one Wi-Fi connection is enough until the cell runs out.
Time zone: `CONFIG_APP_TZ` (menuconfig > Clock and network, default Romania).

| Checkbox | File | Content |
|---|---|---|
| Log raw CAN | `can_<date>.log` | every frame received and sent, candump format (`(1.234000) can0 7E8#04410C1AF8`), opens in SavvyCAN / python-can / `canplayer`. Only `0x7E8..0x7EF` + our requests, or every frame on the bus with the **All traffic** checkbox next to it (the hardware filter is only opened while a raw log is written; a busy car bus can be 1000-2000 frames/s, ~60-80 KB/s on the card) |
| (with Log raw CAN) | `evt_<date>.txt` | what the dash itself did, same time base as the raw log: connection state changes, request timeouts, and once a second (only when something changed) the CAN error counters: failed sends, send retries, dropped frames, bus errors, bus state. A request that failed to go out never appears in `can_NNN.log`; it shows up here |
| Log parameters | `par_<date>.csv` | one row per `CONFIG_DATALOG_PARAM_PERIOD_MS` (200 ms), one column per polled PID, empty cell = no fresh value. A new file starts on every connect or PID selection change |

Files are synced every `CONFIG_DATALOG_SYNC_PERIOD_MS` (2 s); untick the box before
pulling the card. Status line shows `REC` while a log is open.

### Getting the logs over Wi-Fi

No need to pull the card: with the dash on Wi-Fi, open `http://<IP>/` (the Wi-Fi
page shows the address, e.g. `http://192.168.1.57/`) in a browser on the same network.
The page lists the `can_`, `par_`, `evt_` and `dtc_` files on the card by type, newest
first, with date and size, and the free space on the card. Click a name to download it;
delete single files or tick several (or "select all" per type) and **Delete selected**. DBC files for the Dash page are listed too and can be uploaded there (into `/db/dbc/`).
Files being recorded right now are marked *(recording)*: they can be downloaded (contents
up to the last sync) but not deleted. Nothing else on the card is listed or reachable.
There is no password, so anyone on the same network can do this; use it on your own
network or a phone hotspot.

**Auto connect** (Wi-Fi page, on by default): connect to the stored network at boot,
and keep trying while it is out of reach (5 quick attempts, then one every 30 s), also
after the connection is lost. So a dash started in the car connects by itself once it
is back in range of the home network. Disconnect or Forget stops the retries until the
next Connect.

## Enclosure

`CAD/` holds a 3D-printable back cover for the display: a parametric FreeCAD
macro, the STL/STEP exports and preview pictures. It has a chin below the glass
for the car cable, side openings for USB-C / microSD / BOOT / RESET, and an
AMPS mount pattern. Car wiring (KL15 → VIN, KL31 → GND, CAN-H, CAN-L) and what
to measure before printing: see `CAD/README.md`.

## Roadmap

1. Communication layer (done: can_if, obd, comsm, vehicle_data, ui_bridge)
2. Settings in NVS, DTC read/clear (Service 03/07/0A/04), freeze frame (Service 02) (done)
3. Live data screens: Drive / Race / Tune (done)
4. SD card logging (done)
5. Back cover for the car (designed in `CAD/`, waiting for measurements, not printed)
