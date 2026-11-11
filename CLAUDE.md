# OBD_Dash: context for Claude Code sessions

Read this first, then `README.md` (architecture, live screen field/PID tables, logging,
roadmap). This file holds what the README doesn't: how to work in the repo, the
gotchas, and where development stopped.

## Project

An OBD-II dashboard on a **Waveshare ESP32-S3-Touch-LCD-5**: 800x480 RGB panel, GT911
touch, CH422G IO expander, SD card, and TWAI (CAN) at 500 kbit/s.

- **Software:** ESP-IDF v6.1, LVGL 8.3, and a UI designed in EEZ Studio 0.29 using EEZ Flow
  (`EEZ_FOR_LVGL`).
- **Test setup:** the owner tests against a Python car simulator on a PC, with a second
  ESP32 acting as the CAN interface. There is no real car on the bench.
- **Owner:** shadowm60. Their English is informal; read requests for intent.

## Build / flash / UI

```sh
. ~/.espressif/tools/activate_idf_v6.1.sh
ninja -C build                      # normal build
idf.py -B build reconfigure         # after Kconfig / sdkconfig.defaults changes
idf.py -B build flash monitor       # owner usually flashes himself
tools/eez-build.sh                  # regenerate components/hmi/src/ui/ from the EEZ project
```

- **ninja warning:** the VS Code ninja is a different version. A "build log version"
  warning just means a full rebuild.
- **`sdkconfig` is the owner's local file** and is not committed. Put defaults in
  `sdkconfig.defaults`, and mirror needed changes into `sdkconfig` by hand.
- **EEZ headless build:** EEZ Studio is at `~/tools/EEZ-Studio-0.29.0.AppImage`, and the
  script handles its quirks: unset `ELECTRON_RUN_AS_NODE`, `--no-sandbox --disable-gpu
  --ozone-platform=x11`, `APPIMAGE_EXTRACT_AND_RUN=1`. It should print "No error and no
  warning detected" / "Build successfully finished".

## Editing the UI: keep the EEZ project the source of truth

The owner opens `components/hmi/OBD_Dash.eez-project` in EEZ Studio. Treat it as
follows:

1. **Never hand-edit `components/hmi/src/ui/*`.** It is generated.
2. **Change layout by editing the project JSON** with a short Python script. Do it the
   way EEZ stores it:
   - 2-space indent, `ensure_ascii=False`.
   - Fresh UUID `objID`s for cloned widgets.
   - When cloning a widget, drop its `eventHandlers` unless an action really exists.
   - Then run `tools/eez-build.sh`, then `ninja -C build`.
3. **Widgets that C code touches need an `identifier`.** They then appear as
   `objects.<identifier>` in `screens.h`.
4. **Do not commit `OBD_Dash.eez-project-ui-state`.** It is the owner's editor state.

Screens (`ScreensEnum`):

| Screen | Purpose |
|---|---|
| startup | start screen |
| readDTC | DTC read / clear / save |
| clearDTC | unused |
| drive | Drive live view |
| liveData | Race live view |
| tune | Tune live view |
| freezFrame | freeze frame; no longer reachable from any button |
| settings | settings |
| system | system |
| selectParams | Settings > Select Params |

Navigation:
- Use `eez_flow_set_screen` / `eez_flow_get_current_screen`. `loadScreen` does not exist
  in this build.
- Live screens: swipe left/right to cycle Drive -> Race -> Tune; swipe up/down or long
  press to go home (`live_view.c`).

## Architecture rules (see README for the component list)

**Layering.** The layers, bottom to top:

| Layer | Role |
|---|---|
| `can_if` | TWAI driver |
| `isotp` | ISO-TP transport |
| `obd` | client, PID catalogue/scheduler, DTC |
| `comsm` | connection state machine, single task |
| `vehicle_data` | thread-safe signal store |
| `hmi/bridge` | the only code touching LVGL/EEZ, runs in LVGL timers |
| `settings` | NVS blob |
| `datalog` | SD card logging |

Producers never call LVGL.

**PID catalogue** (`obd/obd_pid_table.c`):
- Each entry is pid, data length, rate, signal and decoder.
- Rates: FAST is round-robin, MEDIUM is 500 ms, SLOW is 2000 ms.
- Polling is opt-in. Only PIDs that are selected in Select Params AND supported by the
  ECU are polled.

**Multi-PID polling:**
- Up to 6 PIDs are requested per Service 01 request (`obd_pid_next_batch`). Due timed
  PIDs get at most half the slots.
- comsm falls back to 1 PID per request if the ECU rejects a batch.
- The batch size is set with `comsm_set_pids_per_request` (System > Read speed).

**Settings blob** (`settings.c`):
- Versioned (`SETTINGS_VER`).
- **New fields go at the end of `app_settings_t`.** A shorter blob of the same version
  still loads, and new fields keep their defaults.
- Bump the version only for incompatible changes, because that wipes the owner's
  settings (PID selection).

**Displaying values:**
- **Text tiles:** EEZ string vars `v_*`, formatted in `live_values.c`.
  - Shows `--` when the value is invalid or older than 10 s.
  - Disconnect and deselect invalidate values immediately.
- **Needles:** int vars `rpm_value` and `afr_value` (AFR x100), via `ui_bridge.c`
  bindings.
  - Animated linearly over the measured sample interval. This can be switched off at
    System > Gauge animation.
- **Gauge scaling** (`live_view.c`): lv_meter is integer-only. Gauges run in raw units
  and their tick labels are rewritten.
  - drive_rpm_gauge: x1000
  - rpm_gauge: x100
  - afr_gauge: x`AFR_GAUGE_SCALE` (100)

**Performance:**
- Meter backgrounds are baked once into an `lv_snapshot` image (`bake_background`).
- Labels lying fully inside a meter get frozen into that image. Live values on a gauge
  must therefore sit inside a panel (tile).
- Display: direct mode, avoid-tear mode 3, ~39 Hz vsync, `LV_DISP_DEF_REFR_PERIOD=15`.

**OBD:**
- The default diagnostic session needs **no** Tester Present.
- The idle keepalive (`CONFIG_OBD_KEEPALIVE_MS`) is off by default. The owner does not
  want periodic `01 00` while idle.

## Working conventions

- **Branch:** `restructure`, with PRs to `main`.
- **Commits:** commit when a task is finished and builds, as has been done throughout.
  - Use short messages with a prefix, e.g. `hmi:`, `obd:`, `comsm:`, `readme:`.
  - End each message with the Co-Authored-By line from the session's instructions.
- **Before committing:** every change must build (`ninja -C build`). Keep the README in
  sync, including the live screen tables and the common-changes table.
- **Testing:** the owner tests on hardware. Say clearly what was only built and not run.
  Ask them for the status line (`values/s`) or logs instead of guessing.

## Status (last session ended 2026-11-10)

Done:
- **Phase 1:** communication layer.
- **Phase 2:** settings, DTC, freeze frame.
- **Phase 3:** Drive/Race/Tune live screens.
- **Phase 4:** SD logging. Raw CAN goes to `can_NNN.log` in candump format; parameters
  go to `par_NNN.csv`.
- **Start screen buttons:** Driving View, Race View, Tuning View, Read DTCs, Settings,
  System.

**Built but NOT yet verified on hardware** (ask the owner how they went):
- **First car logs** (2026-11-11, `ObdDash_Logs/`, not committed): the real ECU answers
  in ~10 ms, but the dash had hundreds of 150 ms timeouts and 3.29 s gaps
  (8 timeouts + 2 s reconnect delay), each reconnect starting a new `par_` file.
  Cause: on ESP32-S3 (TWAI v1) `fail_retry_cnt != -1` means single shot, so our
  0x7E0/0x7DF frames were dropped whenever they lost arbitration to car traffic.
  Fix: software retries in `can_tx_task` (16 attempts / 20 ms), plus `evt_NNN.txt`
  event log (with raw logging) showing timeouts, tx_failed/retries, bus state.
  Next drive: check `evt_NNN.txt` for `+tx_failed` and that the gaps are gone.
- Settings > Log CAN RAW > **All traffic** (replaces Kconfig `CAN_ACCEPT_ALL`): opens
  the hardware filter while a raw log is written (`can_if_set_accept_all`, applied in
  the TX task via disable / filter / enable). Never run on a busy bus yet: watch
  `+log_dropped` in `evt_NNN.txt`.
- `385fdc5`: slow PIDs keep their 2 s period. This should stop the `--` flicker on the
  coolant tile.
- `596b450`: needle animation and its checkbox.
- Brightness (`brightness.c`): software dimming layer by default; optional backlight
  PWM via `CONFIG_BACKLIGHT_PWM_GPIO` after a hardware mod (dimming pad by R11/C11,
  inverted duty). The PWM path is derived from the schematic and was never run.
- `4d0ff78`: multi-PID requests (Read speed slider), with Gauge animation and Read speed
  on the System screen.
  - The simulator does answer multi-PID requests (6 PIDs in a 3-frame reply), but
    spaces consecutive frames ~150 ms apart. That broke the 150 ms overall response
    timeout and the ISO-TP library's 100 ms N_Cr, so the dash fell back to 1 PID.
    Fixed after `4d0ff78`: N_Cr is 1000 ms (ISO 15765-2) and the deadline is extended
    on every frame of a multi-frame reply. Not yet re-tested.
    System screen says "ECU accepts 1 PID per request", the simulator needs to support
    `01 0C 0D 05` -> `41 0C xx xx 0D xx 05 xx` (multi-frame ISO-TP).
- **Bus load:** the owner measured ~50 % CAN bus load. Our own traffic should be a few
  percent, so the rest is likely the simulator/bridge. A raw CAN log would show it.
- **FPS:** with logging on, the owner saw ~40 % CPU and 15 FPS before the optimisations.
  It has not been re-measured since.

Open ideas (only on request):
- Freeze Frame button on the Read DTCs screen (the screen exists but is unreachable).
- Delete the unused clearDTC page.
- Auto brightness: needs a light sensor (e.g. LDR on an ADC pin); the checkbox is
  disabled for now.
- Remember the last live view.
- DTC text descriptions.
- PSRAM at 120 MHz for more FPS.
- rusEFI connection method: a dropdown exists, but only OBD is implemented.
