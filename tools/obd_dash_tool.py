#!/usr/bin/env python3
"""OBD_Dash PC tool (roadmap D2): edit the diagnostic database, upload it to the
dash, and try requests through the dash (or the built-in simulator).

    python3 tools/obd_dash_tool.py [database.txt]

Needs only Python 3 with Tk (Linux: package python3-tk). The database format is
docs/diag_db.md; parsing, checking and the simulator are tools/diagdb.py.

Dash connection: the dash's IP (Wi-Fi page) and the PC tool PIN shown on the
dash (Wi-Fi or Diag page). The dash only forwards read requests (3E, 10 01 /
10 03, 22, 19) and only while OBD is disconnected on the dash.
"""
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import diagdb  # noqa: E402

SETTINGS = os.path.join(os.path.expanduser('~'), '.obd_dash_tool.json')


# ---------------------------------------------------------------- dash connection

class DashError(Exception):
    pass


class Dash:
    def __init__(self, ip, pin):
        self.base = f'http://{ip.strip()}'
        self.pin = pin.strip()

    def _call(self, method, path, body=None, timeout=12):
        r = urllib.request.Request(self.base + path, data=body, method=method)
        try:
            with urllib.request.urlopen(r, timeout=timeout) as f:
                return f.read()
        except urllib.error.HTTPError as e:
            text = e.read().decode('utf-8', 'replace')
            try:
                text = json.loads(text).get('error', text)
            except ValueError:
                pass
            raise DashError(f'{e.code}: {text}')
        except (urllib.error.URLError, OSError) as e:
            raise DashError(f'dash not reachable at {self.base}: {e}')

    def status(self):
        return json.loads(self._call('GET', '/api/status', timeout=4))

    def uds(self, tx, rx, req):
        q = urllib.parse.urlencode({'pin': self.pin, 'tx': f'{tx:03X}', 'rx': f'{rx:03X}', 'req': req.hex().upper()})
        r = json.loads(self._call('POST', '/api/uds?' + q, body=b''))
        return r

    def upload(self, name, data):
        self._call('POST', '/up/diag/' + urllib.parse.quote(name), body=data, timeout=20)


def describe(db, ecu, req, rsp):
    """Response bytes -> readable lines."""
    if rsp is None:
        return ['(no answer)']
    out = [rsp.hex(' ').upper()]
    if len(rsp) >= 3 and rsp[0] == 0x7F:
        out.append(f'negative answer to {rsp[1]:02X}: NRC {rsp[2]:02X} {NRC.get(rsp[2], "")}')
    elif rsp[0] == 0x62 and len(rsp) >= 3:
        did = rsp[1] << 8 | rsp[2]
        d = db.did_for(ecu, did) if db else None
        if d:
            out.append(f'{did:04X} {d.name}: {diagdb.decode(d, rsp[3:])}')
        else:
            data = rsp[3:]
            txt = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data)
            out.append(f'{did:04X} (not in the database), {len(data)} bytes, text "{txt}"')
    elif rsp[0] == 0x59 and len(rsp) >= 3 and rsp[1] == 0x02:
        n = (len(rsp) - 3) // 4
        out.append(f'{n} DTC(s)')
        for i in range(n):
            p = rsp[3 + 4 * i: 7 + 4 * i]
            code = int.from_bytes(p[:3], 'big')
            text = db.dtc_text(ecu, code) if db else None
            out.append(f'  {diagdb.format_dtc_code(code)}  status {p[3]:02X}  {text or "(no text)"}')
    return out


NRC = {0x10: 'general reject', 0x11: 'service not supported', 0x12: 'sub-function not supported',
       0x13: 'wrong length', 0x22: 'conditions not correct', 0x31: 'request out of range (not supported)',
       0x33: 'security access denied', 0x78: 'response pending', 0x7E: 'not in this session',
       0x7F: 'service not in this session'}


# ---------------------------------------------------------------- edit dialog

class EditDialog(simpledialog.Dialog):
    """fields: list of (label, initial, choices or None)"""

    def __init__(self, parent, title, fields):
        self.fields = fields
        self.result = None
        super().__init__(parent, title)

    def body(self, master):
        self.vars = []
        for i, (label, init, choices) in enumerate(self.fields):
            ttk.Label(master, text=label).grid(row=i, column=0, sticky='w', padx=4, pady=2)
            v = tk.StringVar(value=init)
            if choices:
                w = ttk.Combobox(master, textvariable=v, values=choices, width=40)
            else:
                w = ttk.Entry(master, textvariable=v, width=42)
            w.grid(row=i, column=1, sticky='we', padx=4, pady=2)
            self.vars.append(v)
            if i == 0:
                first = w
        return first

    def apply(self):
        self.result = [v.get().strip() for v in self.vars]


# ---------------------------------------------------------------- main window

class App(tk.Tk):
    def __init__(self, path=None):
        super().__init__()
        self.title('OBD_Dash tool')
        self.geometry('1000x640')
        self.db = diagdb.Db()
        self.path = None
        self.dirty = False
        self.sim = diagdb.Simulator(self.db)
        self.cfg = self._load_cfg()
        self._build()
        if path:
            self.open_file(path)
        self.protocol('WM_DELETE_WINDOW', self.quit_app)

    # ---- settings
    def _load_cfg(self):
        try:
            with open(SETTINGS) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}

    def _save_cfg(self):
        self.cfg.update(ip=self.ip.get(), last=self.path or '')
        try:
            with open(SETTINGS, 'w') as f:
                json.dump(self.cfg, f)
        except OSError:
            pass

    # ---- layout
    def _build(self):
        bar = ttk.Frame(self)
        bar.pack(fill='x', padx=6, pady=4)
        for text, cmd in (('Open...', self.ask_open), ('Save', self.save), ('Save as...', self.save_as),
                          ('Upload to dash', self.upload)):
            ttk.Button(bar, text=text, command=cmd).pack(side='left', padx=2)
        ttk.Label(bar, text='   Dash IP').pack(side='left')
        self.ip = tk.StringVar(value=self.cfg.get('ip', '192.168.1.50'))
        ttk.Entry(bar, textvariable=self.ip, width=16).pack(side='left', padx=2)
        ttk.Label(bar, text='PIN').pack(side='left')
        self.pin = tk.StringVar()
        ttk.Entry(bar, textvariable=self.pin, width=6).pack(side='left', padx=2)
        ttk.Button(bar, text='Check', command=self.check_dash).pack(side='left', padx=2)

        self.nb = ttk.Notebook(self)
        self.nb.pack(fill='both', expand=True, padx=6)
        self.tables = {}
        self._table('ecus', 'ECUs', ('name', 'tx', 'rx', 'protocol', 'services (sim)'))
        self._table('dids', 'DIDs', ('ecu', 'did', 'name', 'type', 'scale', 'offset', 'unit', 'sim'))
        self._table('dtcs', 'DTCs', ('ecu', 'code', 'text', 'status (sim)'))
        self._console()

        self.status = tk.StringVar(value='Open a database (e.g. sd/db/diag/generic.txt) or start a new one.')
        ttk.Label(self, textvariable=self.status, relief='sunken', anchor='w').pack(fill='x', side='bottom')

    def _table(self, key, title, cols):
        f = ttk.Frame(self.nb)
        self.nb.add(f, text=title)
        tv = ttk.Treeview(f, columns=cols, show='headings', selectmode='browse')
        for c in cols:
            tv.heading(c, text=c)
            tv.column(c, width=90 if c not in ('name', 'text') else 260, stretch=c in ('name', 'text', 'sim'))
        sb = ttk.Scrollbar(f, orient='vertical', command=tv.yview)
        tv.configure(yscrollcommand=sb.set)
        tv.grid(row=0, column=0, sticky='nsew')
        sb.grid(row=0, column=1, sticky='ns')
        f.rowconfigure(0, weight=1)
        f.columnconfigure(0, weight=1)
        bf = ttk.Frame(f)
        bf.grid(row=1, column=0, sticky='w', pady=4)
        ttk.Button(bf, text='Add', command=lambda: self.edit(key, None)).pack(side='left', padx=2)
        ttk.Button(bf, text='Edit', command=lambda: self.edit(key, self._sel(key))).pack(side='left', padx=2)
        ttk.Button(bf, text='Delete', command=lambda: self.delete(key)).pack(side='left', padx=2)
        tv.bind('<Double-1>', lambda e: self.edit(key, self._sel(key)))
        self.tables[key] = tv

    def _console(self):
        f = ttk.Frame(self.nb)
        self.nb.add(f, text='Console')
        top = ttk.Frame(f)
        top.pack(fill='x', pady=4)
        ttk.Label(top, text='ECU').pack(side='left')
        self.ecu_sel = ttk.Combobox(top, width=22, state='readonly')
        self.ecu_sel.pack(side='left', padx=4)
        ttk.Label(top, text='Request (hex)').pack(side='left')
        self.req = tk.StringVar(value='22 F1 90')
        e = ttk.Entry(top, textvariable=self.req, width=24)
        e.pack(side='left', padx=4)
        e.bind('<Return>', lambda ev: self.send())
        ttk.Button(top, text='Send', command=self.send).pack(side='left', padx=2)
        self.local = tk.BooleanVar(value=False)
        ttk.Checkbutton(top, text='built-in simulator (no dash)', variable=self.local).pack(side='left', padx=8)
        self.add_btn = ttk.Button(top, text='Add this DID...', command=self.add_seen_did, state='disabled')
        self.add_btn.pack(side='left', padx=2)
        quick = ttk.Frame(f)
        quick.pack(fill='x')
        for text, req in (('Tester present', '3E 00'), ('VIN', '22 F1 90'), ('Part number', '22 F1 87'),
                          ('Software', '22 F1 89'), ('DTCs', '19 02 FF'), ('Extended session', '10 03')):
            ttk.Button(quick, text=text, command=lambda r=req: (self.req.set(r), self.send())).pack(side='left', padx=2)
        self.log = tk.Text(f, height=20, font=('monospace', 10))
        self.log.pack(fill='both', expand=True, pady=4)
        self.last_did = None

    # ---- database view
    def _rows(self, key):
        if key == 'ecus':
            return [(e.name, f'{e.tx:03X}', f'{e.rx:03X}', e.protocol, ' '.join(f'{s:02X}' for s in e.services))
                    for e in self.db.ecus]
        if key == 'dids':
            return [(d.ecu, f'{d.did:04X}', d.name, d.type, f'{d.scale:g}', f'{d.offset:g}', d.unit, d.sim)
                    for d in self.db.dids]
        return [(d.ecu, diagdb.format_dtc_code(d.code), d.text, '' if d.status is None else f'{d.status:02X}')
                for d in self.db.dtcs]

    def refresh(self):
        for key, tv in self.tables.items():
            tv.delete(*tv.get_children())
            for i, r in enumerate(self._rows(key)):
                tv.insert('', 'end', iid=str(i), values=r)
        names = [e.name for e in self.db.ecus]
        self.ecu_sel['values'] = names
        if names and self.ecu_sel.get() not in names:
            self.ecu_sel.set(names[0])
        self.title(f'OBD_Dash tool - {os.path.basename(self.path) if self.path else "new database"}'
                   f'{" *" if self.dirty else ""}')

    def _sel(self, key):
        s = self.tables[key].selection()
        return int(s[0]) if s else None

    def _list(self, key):
        return {'ecus': self.db.ecus, 'dids': self.db.dids, 'dtcs': self.db.dtcs}[key]

    def edit(self, key, idx):
        lst = self._list(key)
        ecus = ['*'] + [e.name for e in self.db.ecus]
        if key == 'ecus':
            e = lst[idx] if idx is not None else diagdb.Ecu('', 0x7E0, 0x7E8)
            fields = [('Name', e.name, None), ('Request ID (tx, hex)', f'{e.tx:03X}', None),
                      ('Answer ID (rx, hex)', f'{e.rx:03X}', None), ('Protocol', e.protocol, ['uds']),
                      ('Services (sim)', ' '.join(f'{s:02X}' for s in e.services), None)]
        elif key == 'dids':
            e = lst[idx] if idx is not None else diagdb.Did('*', 0xF190, '', 'ascii')
            fields = [('ECU', e.ecu, ecus), ('DID (hex)', f'{e.did:04X}', None), ('Name', e.name, None),
                      ('Type', e.type, list(diagdb.TYPES)), ('Scale', f'{e.scale:g}', None),
                      ('Offset', f'{e.offset:g}', None), ('Unit', e.unit, None), ('Sim value', e.sim, None)]
        else:
            e = lst[idx] if idx is not None else diagdb.Dtc('*', 0x030100, '')
            fields = [('ECU', e.ecu, ecus), ('Code (P0301 / P0301-1C / 030100)', diagdb.format_dtc_code(e.code), None),
                      ('Text', e.text, None), ('Status (sim, hex, empty = text only)',
                                               '' if e.status is None else f'{e.status:02X}', None)]
        d = EditDialog(self, 'Edit' if idx is not None else 'Add', fields)
        if d.result is None:
            return
        try:
            line = ' | '.join(d.result)
            section = {'ecus': 'ecu', 'dids': 'did', 'dtcs': 'dtc'}[key]
            # parse the entry the same way as the file, against the current ECUs
            pre = diagdb.dump(diagdb.Db(ecus=[x for x in self.db.ecus if key != 'ecus' or x is not e]))
            new = diagdb.parse(pre + f'[{section}]\n{line}\n')
            item = _list_of(new, key)[-1]
        except diagdb.DbError as ex:
            messagebox.showerror('Not valid', str(ex), parent=self)
            return
        if idx is None:
            lst.append(item)
        else:
            old = lst[idx]
            lst[idx] = item
            if key == 'ecus' and old.name != item.name:        # rename references
                for x in self.db.dids + self.db.dtcs:
                    if x.ecu.lower() == old.name.lower():
                        x.ecu = item.name
        self.changed()

    def delete(self, key):
        idx = self._sel(key)
        if idx is None:
            return
        lst = self._list(key)
        if key == 'ecus':
            name = lst[idx].name
            used = [x for x in self.db.dids + self.db.dtcs if x.ecu.lower() == name.lower()]
            if used and not messagebox.askyesno('Delete ECU', f'{len(used)} DID / DTC entries belong to {name}. '
                                                'Delete them too?', parent=self):
                return
            self.db.dids = [x for x in self.db.dids if x.ecu.lower() != name.lower()]
            self.db.dtcs = [x for x in self.db.dtcs if x.ecu.lower() != name.lower()]
        del lst[idx]
        self.changed()

    def changed(self):
        self.dirty = True
        self.sim = diagdb.Simulator(self.db)
        self.refresh()

    # ---- files
    def ask_open(self):
        if not self._discard_ok():
            return
        p = filedialog.askopenfilename(filetypes=[('Diagnostic database', '*.txt'), ('All files', '*')],
                                       initialdir=os.path.dirname(self.path or self.cfg.get('last') or '.'))
        if p:
            self.open_file(p)

    def open_file(self, p):
        try:
            self.db = diagdb.load(p)
        except (OSError, diagdb.DbError, ValueError) as e:
            messagebox.showerror('Open', f'{p}:\n{e}', parent=self)
            return
        self.path = p
        self.dirty = False
        self.sim = diagdb.Simulator(self.db)
        self.refresh()
        self.status.set(f'{p}: {len(self.db.ecus)} ECUs, {len(self.db.dids)} DIDs, {len(self.db.dtcs)} DTC texts. '
                        'Saving rewrites the file in the standard layout (comments are not kept).')
        self._save_cfg()

    def save(self):
        if not self.path:
            return self.save_as()
        try:
            text = diagdb.dump(self.db, header=[f'OBD_Dash diagnostic database {os.path.basename(self.path)}',
                                                'format: docs/diag_db.md, written by tools/obd_dash_tool.py'])
            diagdb.parse(text)                                  # must read back
            with open(self.path, 'w', encoding='utf-8') as f:
                f.write(text)
        except (OSError, diagdb.DbError) as e:
            messagebox.showerror('Save', str(e), parent=self)
            return False
        self.dirty = False
        self.refresh()
        self.status.set(f'saved {self.path}')
        self._save_cfg()
        return True

    def save_as(self):
        p = filedialog.asksaveasfilename(defaultextension='.txt', filetypes=[('Diagnostic database', '*.txt')])
        if not p:
            return False
        self.path = p
        return self.save()

    def _discard_ok(self):
        return not self.dirty or messagebox.askyesno('Unsaved changes', 'Discard the unsaved changes?', parent=self)

    def upload(self):
        if not self.path or (self.dirty and not self.save()):
            messagebox.showinfo('Upload', 'Save the database first.', parent=self)
            return
        name = os.path.basename(self.path)
        try:
            with open(self.path, 'rb') as f:
                Dash(self.ip.get(), self.pin.get()).upload(name, f.read())
        except (OSError, DashError) as e:
            messagebox.showerror('Upload', str(e), parent=self)
            return
        self._save_cfg()
        self.status.set(f'uploaded {name} to the dash (db/diag/). On the dash: open the Diag page again to load it.')

    def check_dash(self):
        try:
            st = Dash(self.ip.get(), self.pin.get()).status()
        except DashError as e:
            self.status.set(str(e))
            return
        self._save_cfg()
        sess = st.get('session')
        self.status.set(f'dash reachable: OBD {st.get("obd")}{", API LOCKED (restart the dash)" if st.get("locked") else ""}'
                        f'{", session " + sess + " kept open by the dash (10 01 ends it)" if sess else ""}'
                        f'{"" if st.get("obd") == "idle" else " - disconnect OBD on the dash to use the console"}')

    # ---- console
    def _say(self, text):
        self.log.insert('end', text + '\n')
        self.log.see('end')

    def send(self):
        ecu = self.db.ecu(self.ecu_sel.get()) if self.ecu_sel.get() else None
        if ecu is None:
            messagebox.showinfo('Console', 'Add an ECU first (ECUs tab), or open a database.', parent=self)
            return
        try:
            req = bytes.fromhex(self.req.get())
        except ValueError:
            messagebox.showerror('Console', 'Request: hex bytes, e.g. 22 F1 90', parent=self)
            return
        if not req:
            return
        self._say(f'> {ecu.name} {ecu.tx:03X}: {req.hex(" ").upper()}' + ('   (built-in simulator)' if self.local.get() else ''))
        if self.local.get():
            rsp = self.sim.respond(ecu, req)
        else:
            try:
                r = Dash(self.ip.get(), self.pin.get()).uds(ecu.tx, ecu.rx, req)
            except DashError as e:
                self._say(f'  {e}')
                return
            if r.get('state') == 'done':
                rsp = bytes.fromhex(r.get('data', ''))
            elif r.get('state') == 'negative':
                rsp = bytes([0x7F, req[0], int(r.get('nrc', '0'), 16)])
            else:
                self._say(f'  {r.get("state")} {r.get("error", "")}'.rstrip())
                self.add_btn.configure(state='disabled')
                return
        for line in describe(self.db, ecu, req, rsp):
            self._say('  ' + line)
        if rsp and rsp[0] == 0x50 and len(rsp) >= 2:
            self._say('  session open: ' + ('the dash keeps it alive (tester present every 2 s) until 10 01'
                                           if not self.local.get() else 'built-in simulator: expires 5 s after the last request')
                      if rsp[1] != 0x01 else '  back in the default session')
        if self.local.get():
            for ev in self.sim.events:
                self._say('  [simulator] ' + ev)
            self.sim.events.clear()
        self.last_did = None
        if rsp and rsp[0] == 0x62 and len(rsp) >= 3:
            self.last_did = (ecu, rsp[1] << 8 | rsp[2], rsp[3:])
        self.add_btn.configure(state='normal' if self.last_did else 'disabled')

    def add_seen_did(self):
        if not self.last_did:
            return
        ecu, did, data = self.last_did
        printable = data and all(32 <= b < 127 or b in (0, 0xFF) for b in data)
        typ = 'ascii' if printable else {1: 'u8', 2: 'u16', 4: 'u32'}.get(len(data), 'hex')
        sim = data.rstrip(b' \x00\xff').decode('latin-1') if printable else (
            str(int.from_bytes(data, 'big')) if typ != 'hex' else data.hex(' ').upper())
        self.nb.select(1)
        existing = self.db.did_for(ecu, did)
        if existing and existing.ecu.lower() == ecu.name.lower():
            messagebox.showinfo('Add DID', f'{did:04X} is already in the database for {ecu.name}.', parent=self)
            return
        self.db.dids.append(diagdb.Did(ecu.name, did, f'DID {did:04X}', typ, sim=sim))
        self.changed()
        self.edit('dids', len(self.db.dids) - 1)            # name it, fix the type / scale

    def quit_app(self):
        if self._discard_ok():
            self._save_cfg()
            self.destroy()


def _list_of(db, key):
    return {'ecus': db.ecus, 'dids': db.dids, 'dtcs': db.dtcs}[key]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else None
    App(path).mainloop()


if __name__ == '__main__':
    main()
