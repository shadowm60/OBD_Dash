#!/usr/bin/env python3
"""PC test of tools/diagdb.py: python3 tools/test/test_diagdb.py"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import diagdb  # noqa: E402

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
fails = 0


def check(cond, what):
    global fails
    if not cond:
        print('FAIL', what)
        fails += 1


TEXT = """
[ecu]
Engine | 7E0 | 7E8 | uds | 10 3E 22 19 14
Cluster | 714 | 77E | uds | 3E 22      # only these two
Airbag | 715 | 77F | kwp
[did]
* | F190 | VIN | ascii | | | | WVWZZZ1KZ6W000001
Engine | F40C | Engine speed | u16 | 0.25 | 0 | rpm | 850
Engine | 1000 | Coolant | s8 | 1 | 0 | C | -12
Engine | 1001 | Sweep | u16 | 1 | 0 | | 0..100
Engine | 1002 | Raw | hex | | | | 01 02 0A
Engine | 1003 | No sim | u8
[kwp_id]
Airbag | 80 | ECU name | ascii | | | | SRS
* | 81 | ECU serial | hex | | | | 01 02
[dtc]
* | P0301 | Cylinder 1 misfire detected
Engine | P0301 | Cylinder 1 misfire (engine) | 2F
Engine | U0100-87 | Lost communication with ECM | 08
Airbag | P0301 | Airbag fault | 2F
"""

check(diagdb.parse_dtc_code('P0301') == 0x030100, 'P0301')
check(diagdb.parse_dtc_code('U0100-87') == 0xC10087, 'U0100-87')
check(diagdb.parse_dtc_code('B1234') == 0x923400, 'B1234')
check(diagdb.format_dtc_code(0xC10087) == 'U0100-87', 'format')

db = diagdb.parse(TEXT)
check(len(db.ecus) == 3 and len(db.dids) == 6 and len(db.kwp_ids) == 2 and len(db.dtcs) == 4, 'counts')
eng, clu = db.ecu('engine'), db.ecu_by_tx(0x714)
airbag = db.ecu('airbag')
check(clu.services == [0x3E, 0x22], 'services')
check(airbag.protocol == 'kwp' and airbag.services == diagdb.KWP_DEFAULT_SERVICES and
      airbag.kwp_dtc_req == '18 00 FF 00', 'KWP ECU defaults')
custom_kwp = diagdb.parse('[ecu]\nA | 7E0 | 7E8 | kwp | | 18 02 FF 00\n')
check(custom_kwp.ecus[0].kwp_dtc_req == '18 02 FF 00', 'custom KWP DTC request')
sim = diagdb.Simulator(db, clock=lambda: 5.0)

def r(ecu, hexreq):
    out = sim.respond(ecu, bytes.fromhex(hexreq))
    return out.hex().upper() if out else None

check(r(eng, '3E00') == '7E00', 'tester present')
check(r(eng, '3E80') is None, 'suppress positive response')
check(r(eng, '1003') == '5003003201F4', 'session')
check(r(eng, '22F190') == '62F190' + b'WVWZZZ1KZ6W000001'.hex().upper(), 'VIN')
check(r(clu, '22F190').startswith('62F190'), 'VIN via *')
check(r(eng, '22F40C') == '62F40C0D48', 'rpm 850 / 0.25 = 3400 = 0D48')
check(r(eng, '221000') == '621000F4', 'signed -12')
check(r(eng, '221001') == '6210010019', 'sweep at t=5 s: 25')
check(r(eng, '221002') == '621002' + '01020A', 'hex')
check(r(eng, '221003') == '7F2231', 'no sim value')
check(r(airbag, '1081') == '5081003201F4', 'KWP diagnostic session')
check(r(airbag, '1A80') == '5A80535253', 'KWP ECU identification')
check(r(airbag, '1A81') == '5A810102', 'KWP wildcard identification')
check(r(eng, '22F190F40C') == '62F190' + b'WVWZZZ1KZ6W000001'.hex().upper() + 'F40C0D48', 'two DIDs')
check(r(eng, '22F1') == '7F2213', 'length')
check(r(eng, '1902FF') == '5902FF0301002FC1008708', 'DTCs')
check(r(eng, '190208') == '5902FF0301002FC1008708', 'mask 08: both have bit 3')
check(r(eng, '190201') == '5902FF0301002F', 'mask 01: only 2F')
check(r(airbag, airbag.kwp_dtc_req) == '580103012F', 'KWP DTC service')
check(r(airbag, '14FF00') == '54', 'KWP clear')
check(r(airbag, airbag.kwp_dtc_req) == '5800', 'KWP DTCs cleared')
check(r(eng, '1901FF') == '5901FF010002', 'count')
check(r(clu, '1902FF') == '7F1911', 'cluster has no 19')
check(r(eng, '14FFFFFF') == '54', 'clear')
check(r(eng, '1902FF') == '5902FF', 'cleared')
check(r(eng, '2E1234') == '7F2E11', 'write: not supported')

d = db.did_for(eng, 0xF40C)
check(diagdb.decode(d, bytes.fromhex('0D48')) == '850 rpm', 'decode u16')
check(diagdb.decode(db.did_for(eng, 0x1000), b'\xf4') == '-12 C', 'decode s8')
check(db.dtc_text(eng, 0x030100) == 'Cylinder 1 misfire (engine)', 'ECU text wins')
check(db.dtc_text(clu, 0x030100) == 'Cylinder 1 misfire detected', '* text')
check(db.dtc_text(clu, 0x03011C) == 'Cylinder 1 misfire detected', 'generic failure type')

for bad in ['[did]\n* | F190 | VIN | float', '[ecu]\nA | 7E0 | 7E8\nA | 7E1 | 7E9', '[dtc]\nX | P0301 | t',
            '[ecu]\nA | 7E0 | 7E8 | can', '[kwp_id]\nMissing | 80 | ID | ascii',
            '[ecu]\nA | 7E0 | 7E8 | kwp | | 22 00',
            '[ecu]\nA | 7E0 | 7E8 | kwp | | 1800FF00',
            '[ecu]\nA | 8000 | 7E8', 'Engine | 7E0']:
    try:
        diagdb.parse(bad)
        check(False, f'should fail: {bad!r}')
    except diagdb.DbError:
        pass

gen = diagdb.load(os.path.join(ROOT, 'sd', 'db', 'diag', 'generic.txt'))
check(len(gen.ecus) == 2 and gen.ecu('Engine').rx == 0x7E8, 'generic.txt')

# sessions with the S3 timeout
t = [100.0]
ss = diagdb.Simulator(db, clock=lambda: t[0])
check(ss.session_of(eng) == 1, 'default session')
ss.respond(eng, bytes.fromhex('1003')); check(ss.session_of(eng) == 3, 'extended')
t[0] += 4; check(ss.respond(eng, bytes.fromhex('3E80')) is None, '3E 80 no answer')
t[0] += 4; check(ss.session_of(eng) == 3, 'kept alive by 3E 80')
t[0] += 6; check(ss.session_of(eng) == 1, 'S3 timeout')
check(any('-> default' in e for e in ss.events), 'timeout event')

# writing: dump -> parse gives the same database
for src in (db, gen):
    text = diagdb.dump(src)
    back = diagdb.parse(text)
    check(back == src, 'dump / parse round trip')
check(diagdb.dump(db).count('[ecu]') == 1 and 'U0100-87' in diagdb.dump(db) and '[kwp_id]' in diagdb.dump(db),
      'dump content')
bad_db = diagdb.parse(TEXT)
bad_db.dids[0].name = 'VIN # comment'
try:
    diagdb.dump(bad_db)
    check(False, 'dump must refuse "#" in a value')
except diagdb.DbError:
    pass

print(f'{fails} FAILED' if fails else 'all diagdb tests passed')
sys.exit(1 if fails else 0)
