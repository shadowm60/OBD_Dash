# isotp

ISO-TP (ISO 15765-2) transport layer — vendored third-party library.

This is the [`isotp-c`](https://github.com/lishen2/isotp-c) library (segmenting
and reassembling diagnostic messages larger than one CAN frame). The upstream
documentation is in [README.mkd](README.mkd); this file only covers how it is
wired into OBD_Dash.

## Role in this project

[../obd](../obd)'s `obd_client` uses one `IsoTpLink` to send OBD-II / UDS
requests and reassemble multi-frame responses on the **board CAN** link. (The
ELM327 adapter link does its own ISO-TP, so this library is not used there.)

## Dependencies

`can_if` (through the user shim below). Pure C otherwise.

## Integration

The library calls back into the application through shim functions it does not
provide. In this project those are implemented in the `obd` component:

- `isotp_user_send_can()` → `can_if_send()`
- `isotp_user_get_ms()` → `can_if_now_ms()`
- `isotp_user_debug()` → log

Typical flow inside `obd_client`:

```c
IsoTpLink link;
uint8_t tx_buf[...], rx_buf[OBD_MAX_RESPONSE];
isotp_init_link(&link, tx_id, tx_buf, sizeof tx_buf, rx_buf, sizeof rx_buf);

isotp_send(&link, req, req_len);          /* single frame now, multi-frame via poll */
/* each received CAN frame for rx_id: */
isotp_on_can_message(&link, data, len);
isotp_poll(&link);                        /* call periodically: timeouts, consecutive frames */

uint16_t out;
if (isotp_receive(&link, rx_buf, sizeof rx_buf, &out) == ISOTP_RET_OK) { /* complete */ }
```

## Configuration

Buffer sizes and timeouts are in [isotp_config.h](isotp_config.h). Payloads up
to 4095 bytes are supported by the protocol; OBD_Dash sizes its RX buffer to
`OBD_MAX_RESPONSE` (256 bytes), which covers every diagnostic response the dash
reads.

## Gotchas

- `isotp_send()` only transmits the first frame immediately; **consecutive
  frames go out from `isotp_poll()`**, so the owner task must call `poll()`
  regularly or multi-frame sends stall.
- Not thread safe — one `IsoTpLink` is driven by a single task (comsm's).
- Keep local changes minimal so the vendored library can be re-synced; project
  glue lives in `obd`, not here.
