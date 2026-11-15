# dash_svc

Decodes a CAN broadcast (rusEFI, or any ECU) with a DBC file from the SD card,
for the Dash page.

## What it does

Loads a `.dbc` from the SD card, listens to the bus (listen-only — it never
transmits, so OBD can run at the same time), decodes the messages the DBC knows
in its own task, and exposes the latest value of every signal to the UI.

Two frame sources:

- **board CAN** — it takes `can_if`'s `CAN_IF_MON_DASH` monitor slot and opens
  the hardware filter (`CAN_IF_ALL_DASH`);
- **BLE adapter** — frames are pushed in from comsm's adapter monitor via
  `dash_svc_push_frame()`.

Base ID: an ECU with a configurable CAN base (rusEFI) sends the DBC's messages
shifted. `dash_svc_set_base()` sets where the DBC's lowest 11-bit ID sits on
the bus, and all 11-bit IDs move with it.

## Dependencies

`can_if`, `dbc`, `vfs` (private). Thread safe; values are copied out under a
lock.

## Integration

```c
dash_svc_init();
dash_svc_load("rusefi.dbc");          /* "" = first file in /sdcard/db/dbc */
dash_svc_set_base(0x200);             /* 0 = IDs as in the DBC */

/* board CAN: */   dash_svc_set_active(true, false);
/* BLE adapter: */ dash_svc_set_active(true, true);
                   /* then feed frames: dash_svc_push_frame(id, data, dlc, NULL); */
```

For the adapter's hardware filter, ask which IDs to let through:

```c
uint32_t filt, mask;
if (dash_svc_id_filter(&filt, &mask)) { /* program the adapter / preset */ }
```

## API

Status and values:

```c
dash_svc_status_t st; dash_svc_get_status(&st);   /* file, base, nmsg/nsig, frames, errors */

int n = dash_svc_signal_count();
dash_svc_value_t v;
dash_svc_value(idx, &v);                            /* name, unit, value, age, min/max, decimals */
dash_svc_get("RPM", &val, &age_ms, unit, sizeof unit);
int i = dash_svc_find("RPM");                       /* index by name, -1 none */
```

Listings for the UI:

```c
dash_svc_list_files(buf, len);        /* .dbc files in /sdcard/db/dbc, '\n' separated */
dash_svc_signal_names(buf, len);      /* all signal names, DBC order */
dash_svc_format_signals(buf, len, max_rows);   /* "name  value unit  age" table */
```

## Gotchas

- **Signal indexes change on every `dash_svc_load()`** — `status.generation`
  bumps each load; re-resolve names after a reload.
- `dash_svc_get()` returns false for a signal that is known but never received
  yet — check `received` / `age_ms`, not just the return value.
- It is listen-only and shares the bus with OBD; the bitrate is the CAN page's
  setting, not a separate one.
- The Dash **tile layout** (which signals, limits) lives on the SD card as
  `/sdcard/db/dash/<dbc>.txt` and is handled by the HMI bridge, not here.
