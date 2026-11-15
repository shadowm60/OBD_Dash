# Components

Each component has its own `README.md` (purpose, dependencies, integration,
API, gotchas). This page is the map. For how to build and work in the repo, see
the top-level [README.md](../README.md) and [CLAUDE.md](../CLAUDE.md).

## Layers (bottom → top)

```
            ┌─────────────────────────── hmi (UI, LVGL + EEZ) ───────────────────────────┐
            │  depends on everything below; nothing depends on it                         │
            └───────────────┬─────────────────────────────────────────────┬──────────────┘
   connectivity             │                 features                     │
   ┌──────────┬──────────┐  │  ┌──────────┬──────────┬──────────┬────────┐ │
   │ wifi_svc │ web_svc  │  │  │ datalog  │ canmon   │ dash_svc │ io_svc │ │
   │ ble_svc  │          │  │  └──────────┴────┬─────┴────┬─────┴────────┘ │
   └────┬─────┴──────────┘  │        comsm ────┤          │ dbc / diagdb (pure C, PC-testable)
        │                   └──────────────────┤          │
        │                        obd (obd_client / obd_elm / obd_pids / obd_dtc)
        │                                       │ isotp (vendored)
        │                                   can_if (TWAI)
   vehicle_data (signal store)          settings (NVS)          board (LCD, touch, SD, I2C, RTC)
```

## Index

### Foundation
- **[settings](settings/)** — persistent user config (NVS).
- **[vehicle_data](vehicle_data/)** — thread-safe store of the latest value of every signal.
- **[board](board/)** — hardware drivers (LCD, GT911 touch, SD, CH422G IO expander, RTC).

### CAN transport
- **[can_if](can_if/)** — CAN bus interface (TWAI): TX queue, RX by ID range, monitor taps, stats.
- **[isotp](isotp/)** — ISO-TP multi-frame transport (vendored third-party lib).

### OBD / diagnostics
- **[obd](obd/)** — OBD-II / UDS transport (board CAN or ELM327) + Service-01 PID engine + DTC helpers.
- **[comsm](comsm/)** — communication state machine; the single owner of the car link.
- **[diagdb](diagdb/)** — diagnostic database parser (ECUs / DIDs / DTC texts), pure C.

### Dash (custom gauges)
- **[dbc](dbc/)** — Vector `.dbc` parser + frame decoder, pure C.
- **[dash_svc](dash_svc/)** — decodes a CAN broadcast with a DBC for the Dash page.

### Observability
- **[canmon](canmon/)** — CAN bus monitor (CAN page): stats + per-ID table.
- **[datalog](datalog/)** — SD logging: raw CAN, events, parameter CSV.

### Connectivity
- **[wifi_svc](wifi_svc/)** — Wi-Fi station, reconnect, SNTP time.
- **[ble_svc](ble_svc/)** — BLE central + serial port for BLE ELM327 adapters.
- **[web_svc](web_svc/)** — HTTP log manager + read-only PC-tool UDS API.

### Board IO & UI
- **[io_svc](io_svc/)** — isolated DI/DO on the terminal block, I2C scan, chip temp.
- **[hmi](hmi/)** — the UI: EEZ/LVGL project + the C bridge to the services.

## Conventions across components

- **One owner per link.** `obd_client` is driven only by comsm's task; the UI
  posts commands and reads status snapshots — it never blocks on the bus.
- **Status snapshots + sequence counters.** Services expose a `*_get_status()`
  struct with a `seq` / `change_count` / `generation` field for cheap "did it
  change?" checks, instead of callbacks into the LVGL task.
- **Thread-safe service APIs, non-blocking UI.** All bridge code runs in the
  LVGL task and must return quickly.
- **Pure-C, PC-testable parsers.** `dbc`, `diagdb` (and the ISO-TP glue) build
  on the PC; see `tools/test/`.
- **The SD card** holds logs in the root and databases under `/sdcard/db/`
  (`dbc/`, `diag/`, `dash/`).
