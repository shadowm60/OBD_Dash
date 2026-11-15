# dbc

CAN database (Vector `.dbc`) parser and frame decoder — pure C, no ESP-IDF.

## What it does

Parses a subset of the Vector DBC format and decodes CAN frames into physical
values. Used by [../dash_svc](../dash_svc) to interpret a car's (or rusEFI's)
broadcast on the Dash page.

Supported: `BO_` messages (11-bit and 29-bit, bit 31 = extended), `SG_`
signals with Intel (`@1`) and Motorola (`@0`) byte order, signed/unsigned,
scale + offset, unit, min/max, and simple multiplexing (`M` / `mN`). Ignored:
value tables, attributes, comments; float signals (`SIG_VALTYPE_`) are decoded
as integers.

Because it is pure C with no ESP-IDF dependency, it also builds and runs on the
PC — see the test harness `tools/test/test_dbc.sh`.

## Dependencies

None (standard C + `stdio`).

## Threading

Not thread safe: one owner decodes (dash_svc) and readers copy values out under
that owner's lock.

## Integration

The caller supplies the message and signal tables (the dash puts them in
PSRAM); `dbc_parse()` fills them from an open file:

```c
dbc_t db = {
    .msgs = msg_table, .max_msg = MAX_MSG,
    .sigs = sig_table, .max_sig = MAX_SIG,
};
FILE *f = fopen("/sdcard/db/dbc/rusefi.dbc", "r");
int nsig = dbc_parse(&db, f);        /* < 0 on read error; lines > 512 B skipped */
fclose(f);
```

Configurable base ID (an ECU whose CAN base is movable, e.g. rusEFI): set
`db.id_offset` so that `bus ID = DBC ID + offset` for every 11-bit message.
`dbc_base_id(&db)` returns the lowest 11-bit ID in the file.

Decode frames:

```c
int msg = dbc_decode(&db, id, ext, data, dlc, ts_ms);   /* -1 if ID not in the DB */
/* decoded values land in db.sigs[i].value / .ts_ms */
int s = dbc_find(&db, "RPM");         /* signal index by name, case-insensitive, -1 none */
```

## API summary

| Function | Purpose |
|---|---|
| `dbc_parse(db, f)` | parse a file into caller-provided tables |
| `dbc_decode(db, id, ext, data, dlc, ts)` | decode one frame → signal values |
| `dbc_base_id(db)` | lowest 11-bit message ID, -1 if none |
| `dbc_find(db, name)` | signal index by name |
| `dbc_extract(data, start, len, intel)` | raw bit extraction (exposed for tests) |

## Gotchas

- Lines longer than 512 bytes are skipped and counted in `db.skipped`.
- Multiplexing is simple: `-1` always present, `-2` the multiplexer signal,
  `>= 0` present only for that mux value.
- Float signals are read as integers; don't rely on IEEE-float DBC signals.
