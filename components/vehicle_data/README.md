# vehicle_data

Thread-safe store of the latest value of every vehicle signal.

## What it does

A flat table of named signals (RPM, speed, coolant temp, …) with the newest
value, a timestamp and a valid flag for each. Producers write, consumers read;
nobody blocks on the bus. Signals are declared once in `vehicle_signals.def`
(X-macro) and reached by the generated `VD_*` enum.

Every sample carries `ts_ms`, so a consumer can show stale data as `--` by
passing a `max_age_ms` to `vd_get()`.

No dependency on CAN, OBD or LVGL — this is a pure data hub that decouples the
producers (comsm / obd_pids) from the consumers (UI bridge, datalog).

## Dependencies

`esp_timer` only.

## Integration

Call `vd_init()` once near boot (after `settings_init()`), then producers push
and consumers pull.

Adding a signal: add one `VD_SIGNAL(id, name, unit, min, max, dec)` line to
`vehicle_signals.def`. The enum, metadata table and store size all follow from
it. A PID decoder in [../obd](../obd) references the signal by its `VD_<id>`.

## API

Producer side (comsm / obd_pids):

```c
vd_set(VD_RPM, 2500.0f);
vd_invalidate(VD_RPM);     /* mark unknown, e.g. on disconnect */
vd_invalidate_all();
```

Consumer side (UI, logger):

```c
vd_sample_t s;
if (vd_get(VD_RPM, 2000 /*ms*/, &s))   /* true: valid and fresh */
    show(s.value);

vd_sample_t all[VD_COUNT];
vd_snapshot(all);          /* consistent one-shot copy, for a CSV row */
```

Cheap change detection and metadata:

```c
uint32_t g = vd_generation();          /* bumps on every vd_set() */
const vd_signal_info_t *i = vd_info(VD_RPM);   /* name, unit, min, max, decimals */
```

## Gotchas

- `vd_get()` always fills `out`; the return value tells you whether the value
  is valid and within `max_age_ms`. `max_age_ms = 0` disables the age check.
- The signal set is fixed at compile time. Signals that arrive from a DBC at
  runtime live in [../dash_svc](../dash_svc), not here.
