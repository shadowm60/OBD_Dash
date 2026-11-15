# canmon

CAN bus monitor — the CAN page.

## What it does

Watches the bus and keeps live statistics and a per-ID table for the CAN page:

- statistics: frames/s, peak, estimated bus load %, error counters, bus state;
- a table of every ID seen (RX and our own TX): DLC, last data, count, period;
- bus settings at runtime: bitrate and listen-only (sniff without ACKing).

While running it takes `can_if`'s `CAN_IF_MON_VIEW` slot and keeps the hardware
filter open (all IDs). Everything runs in one task; commands are queued, so all
calls are non-blocking and safe from the LVGL task.

The raw-log side is **not** here — [../datalog](../datalog) writes the log from
its own monitor slot.

## Dependencies

`can_if`, `esp_timer` (private).

## Integration

```c
canmon_init();
canmon_start();                       /* opens the filter, starts counting */
canmon_set_bus(500000, true);         /* restart the bus: bitrate + listen-only */
canmon_stop();                        /* takes effect at once */
canmon_reset();                       /* clear counters and the ID table */
```

## API

```c
canmon_stats_t s; canmon_get_stats(&s);
/* s.running, s.fps, s.fps_peak, s.load_pct, s.ids, s.bus_errors, s.tec/rec, s.state, ... */

int n = canmon_format_ids(buf, len, max_rows);   /* fixed-width ID table, sorted by ID */
canmon_debug_log();                   /* log where the task is (diagnostics) */
```

## Gotchas

- Listen-only is only sensible while OBD is disconnected (it never ACKs); the
  **caller** is responsible for checking that before enabling it.
- `canmon_stop()` is synchronous — it clears the tap, releases "accept all" and
  stops counting itself, so the CAN page can rely on it having taken effect.
- Starting the monitor opens the hardware filter wide (owner
  `CAN_IF_ALL_MONITOR`), which affects the whole bus interface while it runs.
