# comsm

Communication state machine — the single owner of the car link.

## What it does

comsm runs its own task and is the **only** code that drives
[../obd](../obd)'s `obd_client`. Everything else (the UI) posts commands and
reads a status snapshot; nobody else blocks on the bus. It handles:

- the connect lifecycle and automatic reconnect;
- Service-01 PID polling into [../vehicle_data](../vehicle_data);
- OBD diagnostics (DTC read / clear, freeze frame — Service 03/04/07/0A/02);
- raw **UDS** requests for the Diag page (any tx/rx, Service 22/19/10/3E…);
- a **diagnostic session keepalive** (tester present);
- a **broadcast monitor** on the adapter link for the Dash page.

```
IDLE --connect--> CONNECTING --0100 ok--> DISCOVERY --done--> RUNNING
  ^                   |                                          |
  |               no answer                             N timeouts in a row
  |                   v                                          v
  +---disconnect--- RETRY_WAIT <--------------------------------+
                       (auto reconnect after CONFIG_OBD_RECONNECT_DELAY_MS)
```

Because comsm owns the link, UDS requests, the Dash adapter monitor and the
session keepalive all run in its task and **only while OBD polling is idle**.

## Dependencies

`vehicle_data` (public); `can_if`, `obd` (private).

## Integration

```c
comsm_init();                       /* starts the task; after obd/can_if are up */
comsm_set_link(OBD_LINK_ELM);       /* board CAN or BLE adapter, used from next connect */
comsm_connect();  /* or comsm_disconnect() / comsm_toggle() for the UI button */
```

Poll status from the UI:

```c
comsm_status_t st; comsm_get_status(&st);
/* st.state, st.ecu_id, st.request_rate, st.session_open, st.monitoring, ... */
```

## API

### Connection & polling

`comsm_connect()`, `comsm_disconnect()`, `comsm_toggle()`,
`comsm_set_pids_per_request(n)` (1..6; falls back to 1 if the ECU rejects
multi-PID), `comsm_set_link(link)`, `comsm_state_name(state)`.

### Dash broadcast monitor (adapter link)

```c
comsm_set_link_monitor(true, filter, mask, cb, arg);   /* frames → cb in comsm's task */
```

OBD has priority: Connect pauses the monitor, Disconnect resumes it.

### UDS (Diag page) — one raw request at a time

```c
esp_err_t e = comsm_uds_request(0x714, 0x77E, req, len);   /* req ≤ 7 bytes (single frame) */
/* ESP_ERR_INVALID_STATE if OBD is connected or a request is still running */
comsm_uds_result_t r; comsm_uds_get(&r);
/* r.state: IDLE/BUSY/DONE/NEGATIVE/TIMEOUT/ERROR; r.seq bumps per finished request */
```

After a positive answer to `10 xx` (xx≠01) comsm keeps that ECU's session
alive with `3E 80` every 2 s, until `10 01`, a session on another ECU, OBD
Connect, or 10 min idle. The open session is visible in
`comsm_status_t.session_*`.

### OBD diagnostics (Service 03/04/07/0A/02)

```c
comsm_read_dtcs();           /* connects first if needed, then stays connected */
comsm_clear_dtcs();          /* Service 04 — confirm with the user first! */
comsm_read_freeze_frame();
comsm_get_dtc_report(&rep);  /* snapshots; rep.seq bumps when the report changes */
comsm_get_freeze_frame(&ff);
```

## Gotchas

- UDS, diagnostics and the adapter monitor **share the link with PID polling**;
  they run only when OBD is idle. `comsm_uds_request()` returns
  `ESP_ERR_INVALID_STATE` while connected.
- `comsm_clear_dtcs()` (Service 04) turns the MIL off and resets readiness
  monitors — always confirm with the user before calling it.
- Results are snapshots; use the `seq` / `change_count` counters for "did it
  change?" rather than polling the whole struct for differences.
- The internal job sequencer (`comsm_diag.c` / `comsm_diag.h`) is private to
  this component — call the public `comsm_*` functions, not `diag_*`.
