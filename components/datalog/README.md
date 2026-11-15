# datalog

SD card logging — raw CAN, events, and parameter CSV. Runs in its own
low-priority task.

## What it does

Three independent logs on the SD card:

| Log | File | Content |
|---|---|---|
| raw | `can_NNN.log` | every CAN frame seen / sent, candump format `(12.345000) can0 7E8#0441…` |
| events | `evt_NNN.txt` | with the raw log: connection state, timeouts, CAN error counters |
| params | `par_NNN.csv` | one CSV row per `CONFIG_DATALOG_PARAM_PERIOD_MS` with the latest value of every polled PID |

The raw log is written only while OBD is connected **or** the CAN-page monitor
runs (one file per such session). It can capture just the OBD conversation or
every frame on the bus (`datalog_set_all_traffic`). The params file starts anew
on every (re)connect or PID-selection change.

File names/times: once the clock is set (SNTP, kept by the RTC) files are named
`<prefix>_YYYYMMDD_HHMMSS` (local time) and timestamped in Unix time; before
that they are numbered and timestamped in seconds since boot. Files are synced
every `CONFIG_DATALOG_SYNC_PERIOD_MS` and closed when logging is switched off.

## Dependencies

`can_if`, `comsm`, `obd`, `vehicle_data`, `vfs` (private).

## Integration

```c
datalog_init("/sdcard");              /* mount point; logging starts disabled */

datalog_enable_raw(true);
datalog_set_monitor_active(true);     /* tells it the bus monitor is running (raw log gate) */
datalog_set_all_traffic(true);        /* every frame, not just the OBD conversation */
datalog_enable_params(true);
```

All setters are idempotent and safe from any task.

## API

```c
datalog_status_t st; datalog_get_status(&st);
/* st.raw_active, st.params_active, st.raw_file, st.raw_frames, st.raw_dropped,
   st.params_rows, st.error_count, st.evt_active, ... */

bool open = datalog_is_open("can_003.log");   /* true → file is being written, must not be deleted */
```

## Gotchas

- `datalog_is_open()` is how [../web_svc](../web_svc) decides a file may be
  downloaded (contents so far) but **not** deleted — respect it.
- The raw log only writes while there is something to log (OBD connected or the
  monitor active); enabling it alone does nothing until one of those is true.
- `set_all_traffic(true)` opens the CAN hardware filter (owner
  `CAN_IF_ALL_LOG`) while a raw log is being written.
- Params CSV cells are empty when no fresh value was available for that PID in
  the row's period.
