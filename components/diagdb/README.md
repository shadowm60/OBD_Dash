# diagdb

Diagnostic database parser — ECUs, data identifiers and DTC texts of one make.

## What it does

Parses `/db/diag/<make>.txt` (format: [../../docs/diag_db.md](../../docs/diag_db.md))
into tables the Diag page uses to scan a car, read identifiers (Service 22),
decode them, and name DTCs. The columns marked *sim* in the spec (for a bench
simulator) are skipped by this parser.

`tools/diagdb.py` is the reference implementation of the same format and also
powers the bench simulator and the PC tool's console.

Pure C, no ESP-IDF — it also builds on the PC (`tools/test/test_diagdb_c.sh`).

## Dependencies

None (standard C + `stdio`).

## Threading

Not thread safe; the caller provides the tables (PSRAM on the dash).

## Integration

```c
diagdb_t db = {
    .ecus = ecu_tbl, .max_ecu = MAX_ECU,
    .dids = did_tbl, .max_did = MAX_DID,
    .dtcs = dtc_tbl, .max_dtc = MAX_DTC,
};
FILE *f = fopen("/sdcard/db/diag/generic.txt", "r");
if (!diagdb_parse(&db, f)) { /* read error or nothing usable; db.error has the first problem */ }
fclose(f);
```

Bad lines are skipped and counted (`db.skipped`); the first one is described in
`db.error` ("line 12: …").

## API

Lookups (ECU-specific entry wins over a `*` wildcard entry):

```c
int e = diagdb_find_ecu(&db, "Engine");
const diagdb_did_t *d = diagdb_find_did(&db, e, 0xF190);     /* NULL if none */
const char *txt = diagdb_dtc_text(&db, e, 0x030100);         /* NULL if none */
```

DTC and value formatting:

```c
uint32_t code;
diagdb_parse_dtc("P0301", &code);        /* also "P0301-1C" or "030100"; false if malformed */
char buf[10]; diagdb_format_dtc(code, buf);
diagdb_decode(d, data, len, out, sizeof out);   /* 0x62 data bytes → "850 rpm" / "WVW…" / "01 02 0A" */
```

## Data types

`DIAGDB_ASCII`, `DIAGDB_HEX`, and `DIAGDB_U8/U16/U32`, `DIAGDB_S8/S16/S32`
(big-endian, as UDS sends them; physical = raw × scale + offset).

## Gotchas

- `DIAGDB_ALL` (`-1`) is the `*` wildcard ECU (e.g. the standard identification
  DIDs). An ECU-specific line always wins over a `*` line with the same DID.
- The dash reads **only**; this component describes read services. There is no
  write / security-access / routine support, by design.
- Keep this parser and `tools/diagdb.py` in step — they must accept the same
  files. The PC test script checks the C parser against reference files.
