# web_svc

Log file manager and PC-tool API over Wi-Fi (HTTP, port 80).

## What it does

Serves a single self-contained web page (no external assets) for managing the
SD card from a browser on the same network: list the log files (`can_`, `evt_`,
`par_`, `dtc_`) and the Dash/Diag database files, grouped by type and newest
first, download them, delete one or several, and upload a `.dbc` / diag file.

It also exposes a small **PC-tool API**: read-only dash status and one
read-only UDS request at a time, guarded by a per-boot PIN.

## Endpoints

| Method / path | Purpose |
|---|---|
| `GET /` | the page (one HTML file) |
| `GET /api/list` | JSON: card size / free, files `{n, s, open}` |
| `GET /dl/<file>` | download |
| `POST /del/<file>` | delete |
| `POST /up/dbc/<file>` | upload a `.dbc` into `/sdcard/db/dbc/` |
| `POST /up/diag/<file>` | upload a diag `.txt` into `/sdcard/db/diag/` |
| `GET /api/status` | PC tool: dash state (no PIN) |
| `POST /api/uds?pin=&tx=&rx=&req=` | PC tool: one read-only UDS request |

`<file>` is a plain log name in the card root (one of the known prefixes), or
`dbc/<name>.dbc` / `diag/<name>.txt` — no `/`, no `..`.

## Dependencies

`board`, `datalog`, `comsm`, `esp_http_server`, `fatfs`, `esp_timer` (private).

## Integration

```c
wifi_svc_init(...);     /* must be up first (needs esp_netif) */
web_svc_init();         /* starts the server */
bool up = web_svc_running();
const char *pin = web_svc_pin();   /* 4-digit, new each boot; shown on the Wi-Fi + Diag pages */
```

## Security model

- **No password** on the file manager: anyone on the same network can list,
  download, delete and upload.
- The **UDS API** is gated by a 4-digit PIN, new per boot (`esp_random`). A
  wrong PIN costs a 1 s delay; after 10 wrong tries it locks until reboot.
- Only **read** services are allowed over the API (`3E`, `10 01/03`, `22`,
  `19`); the handler rejects anything else.
- `/api/uds` returns `409` while OBD is connected (comsm owns the link).

## Gotchas

- Files currently open for writing (`datalog_is_open()`) can be downloaded
  (contents so far) but **not** deleted — the delete handler enforces this.
- The server task's stack is in PSRAM; files are streamed in 8 KB blocks.
- `cfg.max_uri_handlers` is sized for all the routes above (12) — bump it if
  you add endpoints.
- Never put the owner's identifying data into responses; this server is
  reachable by anyone on the LAN.
