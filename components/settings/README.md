# settings

Persistent user configuration, stored in NVS as one versioned blob.

## What it does

Holds every user-tunable value the firmware keeps across reboots: the OBD link
type, selected PIDs, display brightness, Wi-Fi autoconnect, the last open page,
the Dash/Diag database selection, receive-filter presets, the remembered BLE
adapter, and more (see `app_settings_t` in [settings.h](settings.h)).

Setters update RAM immediately and schedule a single flash write
`SAVE_DELAY_MS` later, so a burst of UI changes (e.g. repeated `+`/`-` taps)
collapses into one NVS write. All calls are thread safe.

## Dependencies

`nvs_flash`, `esp_timer`. No dependency on any other app component — this sits
at the bottom of the stack.

## Integration

Call `settings_init()` **first**, before anything that reads configuration:

```c
settings_init();   /* main.c: before vd_init(), can_if_init(), ... */
```

It initialises NVS (erasing the partition if it is corrupt or from an older
layout) and loads the stored blob, falling back to defaults for any field.

Forward compatibility: new fields are appended to the end of `app_settings_t`.
A shorter stored blob of the same version is loaded as-is and the new fields
keep their defaults, so adding a setting never invalidates existing NVS data.

## API

Read/modify/write the whole struct:

```c
app_settings_t s;
settings_get(&s);
s.brightness = 80;
settings_put(&s);     /* saved later, only if something actually changed */
```

Typed convenience setters exist for common fields, e.g.
`settings_set_brightness()`, `settings_set_obd_link()` (via the struct),
`settings_set_wifi_autoconnect()`, `settings_set_pids_per_request()`.

PID selection (opt-in; only selected **and** supported PIDs are polled):

```c
settings_set_pid_selected(0x0C, true);   /* enable RPM */
bool on = settings_pid_selected(0x0C);
```

The last connected ECU's Service-01 support bitmap is cached so the PID list
can be filtered while disconnected: `settings_set_ecu_supported()` /
`settings_get_ecu_supported()`.

Force a write before a planned restart:

```c
settings_flush();
```

## Gotchas

- `sdkconfig` is the owner's local file and is **not** committed; defaults for
  Kconfig go in `sdkconfig.defaults`. That is separate from this component's
  NVS blob.
- `settings_put()` compares against the current RAM copy and only schedules a
  save if a field differs — calling it on every frame is cheap.
