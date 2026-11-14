#!/usr/bin/env python3
"""Diagnostic database (docs/diag_db.md): parser, checks, decoder and the
simulator side (respond()).

    python3 tools/diagdb.py FILE                    parse, check, print a summary
    python3 tools/diagdb.py FILE ECU REQUEST_HEX    print what the simulator answers

As a module (e.g. in a bench simulator):

    import diagdb
    db = diagdb.load('vw.txt')
    sim = diagdb.Simulator(db)
    ecu = db.ecu_by_tx(0x7E0)
    reply = sim.respond(ecu, bytes.fromhex('22F190'))   # -> bytes or None (no answer)
"""
import sys
import time
from dataclasses import dataclass, field

DEFAULT_SERVICES = [0x10, 0x3E, 0x22, 0x19, 0x14]
TYPES = {'ascii': 0, 'hex': 0, 'u8': 1, 'u16': 2, 'u32': 4, 's8': 1, 's16': 2, 's32': 4}


class DbError(Exception):
    pass


@dataclass
class Ecu:
    name: str
    tx: int
    rx: int
    protocol: str = 'uds'
    services: list = field(default_factory=lambda: list(DEFAULT_SERVICES))


@dataclass
class Did:
    ecu: str            # name or '*'
    did: int
    name: str
    type: str
    scale: float = 1.0
    offset: float = 0.0
    unit: str = ''
    sim: str = ''


@dataclass
class Dtc:
    ecu: str
    code: int           # 24 bit: 2 bytes SAE + failure type byte
    text: str
    status: int = None  # sim: reported with this status if set


@dataclass
class Db:
    ecus: list = field(default_factory=list)
    dids: list = field(default_factory=list)
    dtcs: list = field(default_factory=list)

    def ecu(self, name):
        for e in self.ecus:
            if e.name.lower() == name.lower():
                return e
        return None

    def ecu_by_tx(self, tx):
        for e in self.ecus:
            if e.tx == tx:
                return e
        return None

    def did_for(self, ecu, did):
        """ECU specific line first, then '*'."""
        best = None
        for d in self.dids:
            if d.did == did and d.ecu.lower() == ecu.name.lower():
                return d
            if d.did == did and d.ecu == '*':
                best = best or d
        return best

    def dtc_text(self, ecu, code):
        best = None
        for d in self.dtcs:
            if d.code == code and d.ecu.lower() == ecu.name.lower():
                return d.text
            if d.code == code and d.ecu == '*':
                best = best or d.text
            if best is None and d.code >> 8 == code >> 8 and d.code & 0xFF == 0 and d.ecu in ('*', ecu.name):
                best = d.text           # same SAE code, generic failure type
        return best


# ---------------------------------------------------------------- DTC codes

def parse_dtc_code(s):
    """'P0301' -> 0x030100, 'P0301-1C' -> 0x03011C, '030100' -> 0x030100."""
    s = s.strip().upper()
    if len(s) == 6 and all(c in '0123456789ABCDEF' for c in s):
        return int(s, 16)
    ftb = 0
    if '-' in s:
        s, f = s.split('-', 1)
        ftb = int(f, 16)
    if len(s) != 5 or s[0] not in 'PCBU':
        raise DbError(f'bad DTC code {s!r}')
    top = 'PCBU'.index(s[0])
    num = int(s[1:], 16)
    if num > 0x3FFF or ftb > 0xFF:
        raise DbError(f'bad DTC code {s!r}')
    return ((top << 14 | num) << 8) | ftb


def format_dtc_code(code):
    hi = code >> 8
    s = 'PCBU'[hi >> 14] + f'{hi & 0x3FFF:04X}'
    return s if code & 0xFF == 0 else f'{s}-{code & 0xFF:02X}'


# ---------------------------------------------------------------- parser

def _hex(s, what, lineno, maxv=None):
    try:
        v = int(s, 16)
    except ValueError:
        raise DbError(f'line {lineno}: {what} {s!r} is not hex')
    if maxv is not None and v > maxv:
        raise DbError(f'line {lineno}: {what} {s!r} too big')
    return v


def _num(s, dflt, what, lineno):
    if s == '':
        return dflt
    try:
        return float(s)
    except ValueError:
        raise DbError(f'line {lineno}: {what} {s!r} is not a number')


def parse(text):
    db = Db()
    section = None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if line.startswith('[') and line.endswith(']'):
            section = line[1:-1].strip().lower()
            if section not in ('ecu', 'did', 'dtc'):
                raise DbError(f'line {lineno}: unknown section [{section}]')
            continue
        cols = [c.strip() for c in line.split('|')]
        if section == 'ecu':
            cols += [''] * (5 - len(cols))
            if not cols[0]:
                raise DbError(f'line {lineno}: ECU without a name')
            services = [_hex(x, 'service', lineno, 0xFF) for x in cols[4].split()] or list(DEFAULT_SERVICES)
            db.ecus.append(Ecu(cols[0], _hex(cols[1], 'tx', lineno, 0x7FF), _hex(cols[2], 'rx', lineno, 0x7FF),
                               (cols[3] or 'uds').lower(), services))
        elif section == 'did':
            cols += [''] * (8 - len(cols))
            t = cols[3].lower()
            if t not in TYPES:
                raise DbError(f'line {lineno}: unknown type {cols[3]!r} (one of {", ".join(TYPES)})')
            db.dids.append(Did(cols[0] or '*', _hex(cols[1], 'did', lineno, 0xFFFF), cols[2], t,
                               _num(cols[4], 1.0, 'scale', lineno), _num(cols[5], 0.0, 'offset', lineno),
                               cols[6], cols[7]))
        elif section == 'dtc':
            cols += [''] * (4 - len(cols))
            try:
                code = parse_dtc_code(cols[1])
            except DbError as e:
                raise DbError(f'line {lineno}: {e}')
            status = _hex(cols[3], 'status', lineno, 0xFF) if cols[3] else None
            db.dtcs.append(Dtc(cols[0] or '*', code, cols[2], status))
        else:
            raise DbError(f'line {lineno}: entry before the first [section]')
    check(db)
    return db


def check(db):
    names = set()
    for e in db.ecus:
        if e.name.lower() in names:
            raise DbError(f'ECU {e.name!r} twice')
        names.add(e.name.lower())
        if e.protocol not in ('uds',):
            raise DbError(f'ECU {e.name!r}: protocol {e.protocol!r} not supported yet')
    for d in db.dids + db.dtcs:
        if d.ecu != '*' and d.ecu.lower() not in names:
            raise DbError(f'{type(d).__name__} for unknown ECU {d.ecu!r}')
    for d in db.dids:
        if d.sim and d.type in ('hex',):
            bytes.fromhex(d.sim)


def load(path):
    with open(path, encoding='utf-8') as f:
        return parse(f.read())


# ---------------------------------------------------------------- decoding (dash side)

def decode(did, data):
    """Data bytes of a 62 response -> text."""
    if did.type == 'ascii':
        return data.rstrip(b' \x00\xff').decode('latin-1')
    if did.type == 'hex':
        return data.hex(' ').upper()
    n = TYPES[did.type]
    if len(data) < n:
        return f'(short: {data.hex(" ")})'
    raw = int.from_bytes(data[:n], 'big', signed=did.type.startswith('s'))
    v = raw * did.scale + did.offset
    return f'{v:g} {did.unit}'.strip()


# ---------------------------------------------------------------- simulator side

def _encode_sim(did, now):
    """sim column -> data bytes, None if the DID has no sim value."""
    s = did.sim
    if not s:
        return None
    if did.type == 'ascii':
        return s.encode('latin-1')
    if did.type == 'hex':
        return bytes.fromhex(s)
    if '..' in s:                       # sweep a..b, one step per second over 20 s, up and down
        a, b = (float(x) for x in s.split('..', 1))
        ph = int(now) % 40
        frac = ph / 20 if ph < 20 else (40 - ph) / 20
        v = a + (b - a) * frac
    else:
        v = float(s)
    raw = round((v - did.offset) / (did.scale or 1))
    n = TYPES[did.type]
    signed = did.type.startswith('s')
    lo, hi = (-(1 << (8 * n - 1)), (1 << (8 * n - 1)) - 1) if signed else (0, (1 << (8 * n)) - 1)
    raw = max(lo, min(hi, raw))
    return raw.to_bytes(n, 'big', signed=signed)


class Simulator:
    """Answers requests from the database. Keeps the 'cleared' state per ECU."""

    def __init__(self, db, clock=time.time):
        self.db = db
        self.clock = clock
        self.cleared = set()

    def respond(self, ecu, req):
        """req: request bytes (service + data). Returns the response bytes;
        None = no answer at all (e.g. 3E 80, suppress positive response)."""
        if not req:
            return None
        sid = req[0]
        if sid not in ecu.services:
            return bytes([0x7F, sid, 0x11])
        if sid == 0x3E:
            if len(req) != 2:
                return bytes([0x7F, sid, 0x13])
            return None if req[1] & 0x80 else bytes([0x7E, req[1] & 0x7F])
        if sid == 0x10:
            if len(req) != 2:
                return bytes([0x7F, sid, 0x13])
            return bytes([0x50, req[1], 0x00, 0x32, 0x01, 0xF4])
        if sid == 0x22:
            if len(req) < 3 or len(req) % 2 != 1:
                return bytes([0x7F, sid, 0x13])
            out = bytearray([0x62])
            for i in range(1, len(req), 2):
                did_id = req[i] << 8 | req[i + 1]
                d = self.db.did_for(ecu, did_id)
                data = _encode_sim(d, self.clock()) if d else None
                if data is None:
                    return bytes([0x7F, sid, 0x31])
                out += bytes([req[i], req[i + 1]]) + data
            return bytes(out)
        if sid == 0x19:
            if len(req) != 3 or req[1] not in (0x01, 0x02):
                return bytes([0x7F, sid, 0x12 if len(req) == 3 else 0x13])
            mask = req[2]
            dtcs = [] if ecu.name.lower() in self.cleared else [
                d for d in self.db.dtcs
                if d.status is not None and d.ecu.lower() in ('*', ecu.name.lower()) and d.status & mask]
            if req[1] == 0x01:
                return bytes([0x59, 0x01, 0xFF, 0x01, len(dtcs) >> 8, len(dtcs) & 0xFF])
            out = bytearray([0x59, 0x02, 0xFF])
            for d in dtcs:
                out += d.code.to_bytes(3, 'big') + bytes([d.status])
            return bytes(out)
        if sid == 0x14:
            if len(req) != 4:
                return bytes([0x7F, sid, 0x13])
            self.cleared.add(ecu.name.lower())
            return bytes([0x54])
        return bytes([0x7F, sid, 0x11])


# ---------------------------------------------------------------- command line

def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    try:
        db = load(argv[1])
    except (OSError, DbError, ValueError) as e:
        print(f'{argv[1]}: {e}')
        return 1
    if len(argv) >= 4:
        ecu = db.ecu(argv[2])
        if ecu is None:
            print(f'no ECU {argv[2]!r}')
            return 1
        req = bytes.fromhex(argv[3])
        rsp = Simulator(db).respond(ecu, req)
        print(f'{ecu.tx:03X} {req.hex(" ").upper()}  ->  {ecu.rx:03X} {rsp.hex(" ").upper() if rsp else "(no answer)"}')
        if rsp and rsp[0] == 0x62:
            d = db.did_for(ecu, rsp[1] << 8 | rsp[2])
            print(f'  {d.name}: {decode(d, rsp[3:])}')
        return 0
    print(f'{argv[1]}: OK, {len(db.ecus)} ECUs, {len(db.dids)} DIDs, {len(db.dtcs)} DTC texts '
          f'({sum(d.status is not None for d in db.dtcs)} simulated)')
    for e in db.ecus:
        print(f'  {e.name:<16} tx {e.tx:03X} rx {e.rx:03X} {e.protocol}  services {" ".join(f"{s:02X}" for s in e.services)}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
