# can_if

CAN bus interface on top of the ESP-IDF `esp_twai` driver.

## What it does

Owns the TWAI (CAN 2.0) controller and hides it behind a small API:

- start / stop / restart at runtime (bitrate, listen-only);
- the hardware acceptance filter and bus-off recovery;
- a copied TX queue (caller buffers can be reused immediately);
- RX dispatch into per-subscriber queues **by ID range**;
- **monitor slots** that receive every accepted RX frame and every sent TX
  frame (for logging, the CAN page, and Dash DBC decoding);
- bus statistics (frames, drops, error counters, bus state).

It knows nothing about OBD, the logger or the UI. Higher layers
([../obd](../obd), [../canmon](../canmon), [../dash_svc](../dash_svc),
[../datalog](../datalog)) build on it.

## Dependencies

`esp_driver_twai`, `esp_timer`.

## Integration

```c
can_if_config_t cfg = CAN_IF_CONFIG_OBD_DEFAULT();   /* bitrate from Kconfig, filter 7E8..7EF */
can_if_init(&cfg);     /* same as can_if_start() */
```

The default hardware filter accepts only OBD-II response IDs `0x7E8..0x7EF`.
Open it wider at runtime through the **owner** mechanism (see below) rather
than reconfiguring, so independent features don't fight over the filter.

## API

### Subscriptions (point-to-point RX)

```c
QueueHandle_t q = xQueueCreate(16, sizeof(can_frame_t));
can_if_subscribe(0x7E8, 0x7EF, q);                 /* get this ID range */
can_if_set_subscription_range(q, 0x77E, 0x77E);    /* move the range (UDS) */
```

### Transmit

```c
can_frame_t f = { .id = 0x7E0, .dlc = 8, .data = {...} };
can_if_send(&f, 0 /*ms, 0 = non-blocking*/);
```

### Monitor slots (taps)

Three slots, each a queue that receives **every** accepted RX frame and every
successfully sent TX frame (`tx = true`):

```c
can_if_set_monitor(CAN_IF_MON_LOG,  log_q);    /* datalog raw log */
can_if_set_monitor(CAN_IF_MON_VIEW, mon_q);    /* canmon, CAN page */
can_if_set_monitor(CAN_IF_MON_DASH, dash_q);   /* dash_svc DBC decode */
can_if_set_monitor_filter(0x600, 0x700);       /* software filter on all taps */
can_if_inject_monitor(&frame);                 /* feed in a frame from another link (BLE adapter) */
```

### "Accept all" owners

The hardware filter opens to all IDs while **any** owner wants it, else it
returns to the configured filter:

```c
can_if_set_accept_all(CAN_IF_ALL_DASH, true);   /* Dash needs a broadcast ID outside 7E8..7EF */
can_if_set_accept_all(CAN_IF_ALL_DASH, false);  /* release */
```

Owners: `CAN_IF_ALL_LOG`, `CAN_IF_ALL_MONITOR`, `CAN_IF_ALL_DASH`,
`CAN_IF_ALL_DIAG`.

### Stats / housekeeping

`can_if_get_stats()`, `can_if_reset_stats()`, `can_if_running()`,
`can_if_get_config()`, `can_if_now_ms()` (ISR-safe monotonic ms).

## Gotchas

- **Applying `set_accept_all` stops the controller for a moment** (the TX task
  does it between frames), which can cost one in-flight request. comsm only
  changes it while idle.
- Subscriber and monitor queues drop frames when full; the loss is counted
  (`rx_dropped`, `mon_dropped`) rather than blocking the RX path.
- `listen_only = true` makes `can_if_send()` fail — never ACKs, safe to sniff.
- Frames are **copied** into every queue, so one frame on a busy bus with all
  taps and a wide subscription is several copies; keep queues sized for the
  bus load (see `canmon` load %).
