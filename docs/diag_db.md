# Diagnostic database (Diag page)

One plain-text file per make in `/db/diag/` on the SD card, for example `vw.txt` or
`mercedes.txt`. The same file serves two readers:

- **the dash** (Diag page): which ECUs to scan, which identifiers to read, how to
  decode them, the DTC texts;
- **a simulator** (bench): what each simulated ECU answers. `tools/diagdb.py` parses
  the file and builds the replies (`respond()`), so a simulator can import it.

Columns marked *sim* are only read by the simulator; the dash ignores them.

Only read services are described here: the dash never writes data, never unlocks
security access, never runs routines.

## Layout

```
# comment (also after a value: "7E0  # engine")
[ecu]
Engine   | 7E0 | 7E8 | uds | 10 3E 22 19 14
[did]
*        | F190 | VIN | ascii | | | | WVWZZZ1KZ6W000001
[dtc]
Engine   | P0301 | Cylinder 1 misfire detected | 2F
```

- Sections `[ecu]`, `[did]`, `[dtc]`, in any order, each may appear more than once.
- One entry per line, columns separated by `|`, spaces around them ignored.
  Trailing empty columns can be left out.
- `#` starts a comment (whole line, or the rest of a line).
- Hex numbers without `0x`. Names are case-insensitive.

### [ecu]: control units

| Column | Meaning |
|---|---|
| name | unique name, used in `[did]` / `[dtc]` and shown on the dash |
| tx | request CAN ID (11-bit hex), the dash sends to this |
| rx | response CAN ID (11-bit hex), the ECU answers on this |
| protocol | `uds` (ISO 14229 over ISO-TP); later maybe `kwp` |
| services *(sim)* | service IDs the simulated ECU answers, hex, space separated, e.g. `10 3E 22 19 14`. Others get NRC 0x11 (service not supported). Empty = `10 3E 22 19 14` |

### [did]: data identifiers (service 0x22, ReadDataByIdentifier)

| Column | Meaning |
|---|---|
| ecu | ECU name, or `*` = every ECU (e.g. the standard identification IDs) |
| did | identifier, 4 hex digits |
| name | shown on the dash |
| type | how the data bytes are decoded, see below |
| scale | physical = raw x scale + offset (numeric types; empty = 1) |
| offset | (empty = 0) |
| unit | e.g. `rpm`, `C`, `V` |
| sim *(sim)* | the value the simulator returns, see below; empty = the simulator answers NRC 0x31 (request out of range) |

An ECU-specific line wins over a `*` line with the same DID.

The dash's **Identification** button reads every DID in F180..F1FF that applies to the
ECU (its own lines and `*` lines), in file order.

Types (multi-byte numbers are big-endian, as UDS sends them):

| type | data bytes | decoded as | sim value |
|---|---|---|---|
| `ascii` | any | text (trailing spaces / 0x00 / 0xFF removed) | the text |
| `hex` | any | hex bytes | hex bytes, e.g. `01 02 0A` |
| `u8` `u16` `u32` | 1 / 2 / 4 | unsigned x scale + offset | physical value, e.g. `850`; or a sweep `800..6000` (changes every second) |
| `s8` `s16` `s32` | 1 / 2 / 4 | signed (two's complement) | as above |

The simulator turns a physical sim value back into raw: raw = (value - offset) / scale,
rounded.

### [dtc]: trouble code texts, simulated faults

| Column | Meaning |
|---|---|
| ecu | ECU name or `*` (text for every ECU) |
| code | `P0301` (SAE style, failure type 00), `P0301-1C` (with failure type byte), or 6 hex digits `030100` |
| text | shown on the dash |
| status *(sim)* | if set (hex status byte, e.g. `2F`), the simulated ECU reports this DTC with that status; empty = text only |

A UDS DTC is 3 bytes: the SAE code in the first two (P = 00, C = 01, B = 10, U = 11 in the
top two bits, then the 14 bits of the number, e.g. P0301 = 03 01), and a failure type byte.

Status bits (ISO 14229): 0x01 test failed, 0x02 test failed this cycle, 0x04 pending,
0x08 confirmed, 0x10 not completed since clear, 0x20 failed since clear, 0x40 not
completed this cycle, 0x80 warning indicator (MIL) requested.

## What the dash sends, what the simulator answers

| Request | Meaning | Answer (`respond()`) |
|---|---|---|
| `3E 00` | tester present (ECU scan) | `7E 00` |
| `10 01` / `10 03` | default / extended session | `50 xx 00 32 01 F4` (P2 50 ms, P2* 5000 ms); a non-default session falls back to default 5 s (S3) after the last request |
| `3E 80` | tester present, no answer wanted (the dash sends it every 2 s while a session is open) | nothing; keeps the session alive |
| `22 hh ll` | read DID | `62 hh ll` + data; unknown or no sim value: `7F 22 31` |
| `19 02 mm` | DTCs by status mask | `59 02 FF` + per DTC (status & mm != 0): 3 DTC bytes + status |
| `19 01 mm` | number of DTCs | `59 01 FF 01 nn nn` |
| `14 FF FF FF` | clear all DTCs | `54`; the simulator forgets its DTCs until restart |
| service not in the ECU's list | | `7F sid 11` |
| wrong length | | `7F sid 13` |

Example session:

```
dash -> 7E0  3E 00          sim -> 7E8  7E 00
dash -> 7E0  22 F1 90       sim -> 7E8  62 F1 90 57 56 57 5A 5A 5A 31 4B 5A 36 57 30 30 30 30 30 31
dash -> 7E0  19 02 FF       sim -> 7E8  59 02 FF 03 01 00 2F
```

## Editing

`tools/obd_dash_tool.py` edits the file in tables, uploads it to the dash and has a
console to try requests (through the dash or against the built-in simulator), see the
README. A file saved by the tool is rewritten in the standard layout; comments are not
kept.

## Checking a file

```sh
python3 tools/diagdb.py sd/db/diag/generic.txt                 # parse, check, summary
python3 tools/diagdb.py sd/db/diag/generic.txt Engine 22F190   # what the simulator answers
```
