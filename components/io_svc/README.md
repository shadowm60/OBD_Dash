# io_svc

The board's isolated digital IO, the I2C bus scan, and the chip temperature
(the IO page).

## What it does

Exposes the green terminal block's isolated IO plus two diagnostics:

| Signal | Detail |
|---|---|
| `DI0` / `DI1` | optocoupler inputs, 5–36 V, either polarity (NPN/PNP/dry contact). Read via the CH422G (EXIO0 / EXIO5). Active = current flows. |
| `DO0` / `DO1` | optocoupler open-drain outputs, 5–36 V, ≤ 450 mA each (CH422G OC0 / OC1). Off at power-up, not stored. |
| chip temp | ESP32-S3 internal temperature sensor |
| I2C scan | async scan of the I2C bus (for bring-up / debugging) |

All calls are non-blocking and thread safe; poll `io_svc_get_status()`.

## Dependencies

`board`, `esp_driver_tsens`, `esp_driver_i2c`, `esp_timer` (private).

## Integration

```c
io_svc_init();
io_svc_set_fast(true);        /* IO page open: poll DI0/DI1 every 100 ms */
io_svc_set_do(0, true);       /* drive DO0 on */
io_svc_i2c_scan();            /* async; watch i2c_scan_count */
io_svc_reset();               /* clear DI change counters */

io_svc_status_t st; io_svc_get_status(&st);
/* st.di[0..1], st.di_changes[], st.do_on[], st.chip_temp_c, st.i2c_result, ... */
```

## Gotchas

- **Reading the DI inputs briefly disturbs the other CH422G pins.** Each read
  releases all CH422G IO lines for ~0.3 ms (SD CS, LCD reset, backlight
  enable), so inputs are only polled while `io_svc_set_fast(true)` (IO page
  open). Don't leave fast mode on globally — see `board/i2c_io.c`.
- Outputs are **not** persisted; they are off after every power-up.
- The chip temperature is the SoC's, not an ambient/engine sensor.
