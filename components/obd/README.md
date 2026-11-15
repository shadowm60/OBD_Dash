# obd

OBD-II / UDS transport and the Service-01 PID engine.

This component is the car-communication layer below [../comsm](../comsm). It has
four parts:

| File | Role |
|---|---|
| `obd_client.c` | one-request-at-a-time OBD-II / UDS transport over ISO-TP, on either link |
| `obd_elm.c` | ELM327 backend of `obd_client` (serial adapter over BLE) |
| `obd_pids.c` + `obd_pid_table.c` | Service-01 PID catalogue, support/enable state, poll scheduler, decoders |
| `obd_dtc.c` | Diagnostic Trouble Code encode/parse (SAE J2012) |

## Dependencies

`vehicle_data` (public); `can_if`, `isotp`, `esp_timer` (private).

## Threading

**`obd_client` is not thread safe by design** — it is owned and driven by a
single task ([../comsm](../comsm)). The `obd_pids` state accessors *are* thread
safe (the UI reads/toggles while comsm polls).

## obd_client — transport

Two links, same request/result API:

```c
obd_client_init();
obd_client_set_link(OBD_LINK_CAN);     /* board TWAI (default) ... */
obd_client_set_link(OBD_LINK_ELM);     /* ... or ELM327 over a serial link */
obd_client_set_elm_io(&io);            /* ble_svc_serial_* wrapped in obd_elm_io_t */
```

Addressing (ISO 15765-4, 11-bit):

```c
obd_client_set_functional();           /* request to 0x7DF, first responder in 7E8..7EF wins */
obd_client_set_physical(0x7E8);        /* only this ECU */
obd_client_set_ids(0x714, 0x77E);      /* arbitrary tx/rx pair for UDS diagnostics */
```

Request / poll cycle (single owner task):

```c
obd_client_request(req, len);          /* fails if one is already pending */
obd_req_status_t st = obd_client_process(wait_ms);   /* pump frames + timers */
if (st == OBD_REQ_DONE) {
    uint16_t n; const uint8_t *r = obd_client_response(&n);
} else if (st == OBD_REQ_NEGATIVE) {
    uint8_t nrc = obd_client_nrc();
}
```

`DONE`/`NEGATIVE`/`TIMEOUT`/`ERROR` are reported once, then the client returns
to `IDLE`. `obd_client_abort()` drops a pending request.

Broadcast monitor through the ELM327 adapter (the board CAN link gets frames
directly via `can_if`, so it is not needed there):

```c
obd_client_monitor_start(filter, mask);
obd_client_monitor_poll(wait_ms, cb, arg);   /* frames → cb; false = stopped */
obd_client_monitor_stop();
```

`obd_client_link_error()` returns the last link problem (e.g. "adapter not
connected", "CAN ERROR"), `""` if none.

## obd_elm — ELM327 backend

Internal to `obd_client`; not called directly by the app. It owns the AT setup
(`ATZ ATE0 ATL0 ATS0 ATH1 ATSP6 ATAT1`), appends a response-count hex digit for
speed, resends without it when a multi-frame answer comes back short, and
implements the `ATMA` monitor. See `obd_elm.c` for the Vgate iCar Pro 2S
specifics. Re-inits automatically when the BLE serial reconnects.

## obd_pids — Service 01 engine

The catalogue (`obd_pid_table.c`) is `const` in flash; only PIDs with a decoder
are listed. The ECU's reported support, the user's selection, and the poll
scheduler combine so that **only PIDs that are both supported and enabled are
polled** (PID `0x01` is always polled when supported).

```c
obd_pid_reset_supported();
bool more = obd_pid_apply_support_bitmap(0x00, bitmap);   /* answer to PID 00/20/40/... */
obd_pid_set_enabled(idx, true);
int idxs[OBD_MAX_PIDS_PER_REQUEST];
size_t n = obd_pid_next_batch(now_ms, settings_pids, idxs);   /* what to ask next */
obd_pid_mark_polled(idx, now_ms);
size_t decoded = obd_pid_handle_response(resp, len);          /* 41 .. → vehicle_data */
```

## obd_dtc — trouble codes

```c
char s[6]; obd_dtc_format(code, s);                 /* 0x0133 → "P0133" */
uint16_t codes[32];
int n = obd_dtc_parse(resp, len, codes, 32);        /* Service 03/07/0A → codes[] */
```

## Gotchas

- Only call `obd_client_*` from the owning task; the UI must go through comsm.
- On the ELM link the fast path is answers of one CAN frame (≤7 bytes); larger
  Service-01 batches are trimmed by comsm (`obd_client_batch_bytes()`).
- `set_link` only re-inits the ELM on a real change, so toggling Connect does
  not pay the ~1 s adapter setup each time.
