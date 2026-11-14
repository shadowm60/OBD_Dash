#!/usr/bin/env python3
"""
(Re)builds the menu pages of components/hmi/OBD_Dash.eez-project in the
Esp32_monitor look: dark theme, sidebar with the page buttons, title and
status line on every page.

    python3 tools/obd_ui.py && tools/eez-build.sh

What it does to the project, in place:
  - writes the pages in MENU_PAGES and SUB_PAGES (replacing them if present);
  - removes the old menu pages (OLD_PAGES);
  - keeps every other page as it is: drive / liveData / tune (the full-screen
    live views) are only designed in EEZ Studio;
  - adds missing global variables and native actions, never removes variables
    (other pages may use them).

So: the full-screen live views can be edited in EEZ Studio freely. Edits made
in EEZ Studio to the menu pages are overwritten by the next run; change the
builders below instead (or stop using this script for that page).

Widget JSON comes from tools/eez_templates.json (as EEZ Studio 0.29 writes
it), with fresh objIDs. Taken from ~/git/Esp32_monitor/tools/eez_scaffold.py.
The firmware reaches variables, actions and screens by name
(components/hmi/bridge: uivar.c, nav.c, actions.c).
"""
import copy
import json
import os
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
TEMPLATES = os.path.join(HERE, 'eez_templates.json')
PROJECT = os.path.join(HERE, '..', 'components', 'hmi', 'OBD_Dash.eez-project')

# ------------------------------------------------------------------ look (Esp32_monitor)
BG = '#0b0f14'
SIDEBAR = '#111821'
PANEL = '#161e28'
BORDER = '#253041'
ACCENT = '#2d7ff9'
BTN = '#22303f'
DANGER = '#8a2a2a'
WHITE = '#ffffff'
GREY = '#8a96a3'

SIDEBAR_W = 120
NAV_STEP = 46                # sidebar button pitch (10 pages in 480 px, one line each)
NAV_H = 40
CX = SIDEBAR_W + 10          # content x
CW = 800 - CX - 10           # content width
TOP = 48                     # content y
CH = 480 - TOP - 8           # content height

# LVGL symbols (in the Montserrat built-in fonts)
SYM_HOME = ''
SYM_OBD = ''           # LV_SYMBOL_CHARGE
SYM_WARN = ''          # LV_SYMBOL_WARNING
SYM_CAN = ''           # LV_SYMBOL_SHUFFLE
SYM_WIFI = ''
SYM_BT = ''
SYM_IO = '\uf011'            # LV_SYMBOL_POWER (only LVGL's built-in symbols are in the fonts)
SYM_DASH = '\uf06e'          # LV_SYMBOL_EYE_OPEN
SYM_DIAG = '\uf00b'          # LV_SYMBOL_LIST
SYM_DISP = ''          # LV_SYMBOL_SETTINGS

# sidebar pages: screen name, sidebar text, title
MENU_PAGES = [
    ('home', SYM_HOME + ' Home', 'OBD Dash'),
    ('obd', SYM_OBD + ' OBD', 'OBD scanner'),
    ('dash', SYM_DASH + ' Dash', 'Custom dash (CAN / DBC)'),
    ('readDTC', SYM_WARN + ' DTC', 'Trouble codes'),
    ('diag', SYM_DIAG + ' Diag', 'Diagnostics (UDS)'),
    ('can', SYM_CAN + ' CAN', 'CAN bus'),
    ('wifi', SYM_WIFI + ' Wi-Fi', 'Wi-Fi'),
    ('ble', SYM_BT + ' BLE', 'Bluetooth LE'),
    ('io', SYM_IO + ' IO', 'Inputs / outputs'),
    ('display', SYM_DISP + ' Display', 'Display & system'),
]
# pages without their own sidebar button: screen name, parent page, title
SUB_PAGES = [
    ('select_params', 'obd', 'Select parameters'),
    ('freezFrame', 'readDTC', 'Freeze frame'),
    ('ble_console', 'ble', 'BLE adapter console'),
    ('dash_setup', 'dash', 'Dash settings'),
    ('dash_signals', 'dash', 'Dash signals'),
    ('dash_tile', 'dash', 'Dash tile'),
    ('diag_ecu', 'diag', 'Diagnostics: ECU'),
]
# replaced by the pages above
OLD_PAGES = ['startup', 'clearDTC', 'settings', 'system']
# screen order in the project (= ScreensEnum); the first one is shown at the first boot
ORDER = ['home', 'obd', 'readDTC', 'select_params', 'freezFrame', 'drive', 'liveData', 'tune',
         'can', 'wifi', 'ble', 'io', 'display', 'dash', 'diag', 'ble_console', 'dash_setup', 'dash_signals', 'dash_tile', 'diag_ecu']

# name -> (type, default expression). Existing variables are kept as they are.
VARIABLES = {
    'sys_status': ('string', '""'),
    # home
    'home_info': ('string', '"..."'),
    'home_live': ('string', '"..."'),
    'home_services': ('string', '"..."'),
    # obd (conn_status / conn_button_text exist already)
    'conn_status': ('string', '"Not connected"'),
    'conn_button_text': ('string', '"Connect"'),
    'obd_detail': ('string', '""'),
    'obd_mil_text': ('string', '""'),
    'obd_link_sel': ('integer', '0'),
    'link_status': ('string', '""'),
    'link_filter_sel': ('integer', '0'),
    'link_filter_id': ('string', '""'),
    'link_filter_mask': ('string', '""'),
    'obd_log_params': ('boolean', 'false'),
    'obd_read_speed': ('integer', '6'),
    'obd_speed_text': ('string', '""'),
    # dtc
    'read_dtc_text': ('string', '""'),
    # can
    'can_log_raw': ('boolean', 'false'),
    'can_log_all': ('boolean', 'false'),
    'can_log_info': ('string', '""'),
    'can_bitrate_sel': ('integer', '2'),
    'can_listen_only': ('boolean', 'false'),
    'can_run_text': ('string', '"Start monitor"'),
    'can_stats': ('string', '"monitor stopped"'),
    'can_load': ('integer', '0'),
    'can_load_text': ('string', '""'),
    'can_ids': ('string', '""'),
    # io
    'io_di0_text': ('string', '"DI0: -"'),
    'io_di1_text': ('string', '"DI1: -"'),
    'io_di_info': ('string', '""'),
    'io_do0_text': ('string', '"DO0: off"'),
    'io_do1_text': ('string', '"DO1: off"'),
    'io_chip_text': ('string', '""'),
    'i2c_text': ('string', '"not scanned yet"'),
    # wifi
    'wifi_list': ('string', '"(press Scan)"'),
    'wifi_sel': ('integer', '0'),
    'wifi_pass': ('string', '""'),
    'wifi_scan_text': ('string', '""'),
    'wifi_status': ('string', '"..."'),
    'wifi_autoconnect': ('boolean', 'true'),
    # ble
    'ble_list': ('string', '"(press Scan)"'),
    'ble_sel': ('integer', '0'),
    'ble_scan_text': ('string', '""'),
    'ble_status': ('string', '"..."'),
    'ble_services': ('string', '""'),
    'elm_log': ('string', '""'),
    'dash_status': ('string', '""'),
    'diag_files': ('string', '""'),
    'diag_file_sel': ('integer', '0'),
    'diag_scan_text': ('string', '"Scan ECUs"'),
    'diag_status': ('string', '""'),
    'diag_ecus': ('string', '""'),
    'diag_ecu_title': ('string', '""'),
    'diag_ecu_text': ('string', '""'),
    'dash_signals': ('string', '""'),
    'dash_files': ('string', '""'),
    'dash_file_sel': ('integer', '0'),
    'dash_base_text': ('string', '""'),
    'dash_setup_info': ('string', '""'),
    'dash_tile_sel': ('integer', '0'),
    'dash_edit_title': ('string', '""'),
    'dash_edit_info': ('string', '""'),
    'dash_wlo': ('string', '""'),
    'dash_whi': ('string', '""'),
    'dash_alo': ('string', '""'),
    'dash_ahi': ('string', '""'),
    'dash_sig_list': ('string', '""'),
    'dash_sig_sel': ('integer', '0'),
    'dash_t1_name': ('string', '""'),
    'dash_t1_value': ('string', '""'),
    'dash_t1_unit': ('string', '""'),
    'dash_t2_name': ('string', '""'),
    'dash_t2_value': ('string', '""'),
    'dash_t2_unit': ('string', '""'),
    'dash_t3_name': ('string', '""'),
    'dash_t3_value': ('string', '""'),
    'dash_t3_unit': ('string', '""'),
    'dash_t4_name': ('string', '""'),
    'dash_t4_value': ('string', '""'),
    'dash_t4_unit': ('string', '""'),
    'dash_t5_name': ('string', '""'),
    'dash_t5_value': ('string', '""'),
    'dash_t5_unit': ('string', '""'),
    'dash_t6_name': ('string', '""'),
    'dash_t6_value': ('string', '""'),
    'dash_t6_unit': ('string', '""'),
    'dash_t7_name': ('string', '""'),
    'dash_t7_value': ('string', '""'),
    'dash_t7_unit': ('string', '""'),
    'dash_t8_name': ('string', '""'),
    'dash_t8_value': ('string', '""'),
    'dash_t8_unit': ('string', '""'),
    'dash_t9_name': ('string', '""'),
    'dash_t9_value': ('string', '""'),
    'dash_t9_unit': ('string', '""'),

    'elm_info': ('string', '""'),
    'elm_cmd': ('string', '""'),
    # display
    'disp_brightness': ('integer', '100'),
    'disp_brightness_text': ('string', '"Brightness 100 %"'),
    'disp_timeout_sel': ('integer', '0'),
    'disp_gauge_anim': ('boolean', 'true'),
    'sd_text': ('string', '"..."'),
    'sys_version': ('string', '""'),
}

ACTIONS = [
    'nav_home', 'nav_obd', 'nav_readdtc', 'nav_can', 'nav_wifi', 'nav_ble', 'nav_io', 'nav_display',
    'nav_params', 'nav_freeze', 'nav_dash', 'nav_diag',
    'conn_button_pressed', 'obd_drive', 'obd_race', 'obd_tune',
    'can_start_stop', 'can_reset',
    'io_do0_toggle', 'io_do1_toggle', 'io_reset', 'i2c_scan',
    'wifi_scan', 'wifi_connect', 'wifi_disconnect', 'wifi_forget',
    'ble_scan', 'ble_connect', 'ble_disconnect', 'ble_forget',
    'diag_scan', 'diag_read_info', 'diag_read_dtc', 'diag_clear_dtc', 'diag_save',
    'dash_reload', 'nav_dash_setup', 'dash_apply', 'nav_dash_signals', 'dash_tile_save', 'dash_tile_clear',
    'nav_ble_console', 'elm_atz', 'elm_ati', 'elm_atrv', 'elm_0100', 'elm_send', 'elm_clear',
    'sd_remount', 'screen_off', 'restart',
]
# actions of the old pages, removed when nothing uses them any more
OLD_ACTIONS = ['log_raw_enabled', 'log_params_enabled', 'obd_srv_screen_loaded']


def uid():
    return str(uuid.uuid4())


def fresh_ids(o):
    if isinstance(o, dict):
        if 'objID' in o:
            o['objID'] = uid()
        for v in o.values():
            fresh_ids(v)
    elif isinstance(o, list):
        for v in o:
            fresh_ids(v)
    return o


class B:
    """Widget builder on top of the EEZ templates (from Esp32_monitor)."""

    def __init__(self, tpl):
        self.tpl = tpl['widgets']

    def base(self, wtype, x, y, w, h, styles):
        t = copy.deepcopy(self.tpl[wtype])
        t.update(left=x, top=y, width=w, height=h, leftUnit='px', topUnit='px',
                 widthUnit='px', heightUnit='px', children=[], eventHandlers=[],
                 hiddenFlagType='literal', group='', groupIndex=0)
        t.pop('hiddenFlag', None)
        t.pop('identifier', None)
        t['localStyles'] = {'objID': uid(), 'definition': styles}
        t['style'] = {'objID': uid(), 'useStyle': 'default', 'conditionalStyles': [], 'childStyles': []}
        return fresh_ids(t)

    @staticmethod
    def hide(t, expr):
        if expr:
            t['hiddenFlagType'] = 'expression'
            t['hiddenFlag'] = expr
        return t

    @staticmethod
    def on_click(t, action):
        t['eventHandlers'].append({'objID': uid(), 'eventName': 'CLICKED', 'handlerType': 'action',
                                   'action': action, 'userData': 0})
        return t

    def label(self, text, x, y, w=None, font=16, color=WHITE, expr=False, wrap=False, align=None, hidden=None):
        st = {'text_color': color, 'text_font': f'MONTSERRAT_{font}' if isinstance(font, int) else font}
        if align:
            st['text_align'] = align
        t = self.base('LVGLLabelWidget', x, y, w or 0, 30, {'MAIN': {'DEFAULT': st}})
        t['widthUnit'] = 'px' if w else 'content'
        t['heightUnit'] = 'content'
        t.update(text=text, textType='expression' if expr else 'literal',
                 longMode='WRAP' if wrap else 'CLIP', recolor=False)
        return self.hide(t, hidden)

    def panel(self, x, y, w, h, children=(), bg=PANEL, scroll=False, radius=8, pad=12, hidden=None, border=True):
        st = {'pad_top': pad, 'pad_bottom': pad, 'pad_left': pad, 'pad_right': pad,
              'bg_color': bg, 'bg_opa': 255, 'radius': radius, 'text_color': WHITE,
              'border_width': 1 if border else 0, 'border_color': BORDER}
        t = self.base('LVGLPanelWidget', x, y, w, h, {'MAIN': {'DEFAULT': st}})
        if scroll:
            t['widgetFlags'] = 'GESTURE_BUBBLE|PRESS_LOCK|SCROLLABLE|SCROLL_ELASTIC|SCROLL_MOMENTUM|SNAPPABLE'
            t['flagScrollDirection'] = 'ver'
            t['flagScrollbarMode'] = 'auto'
        t['children'] = list(children)
        return self.hide(t, hidden)

    def button(self, text, x, y, w, h, action, color=BTN, font=20, expr=False, hidden=None):
        """action None: no EEZ action, the bridge hooks the button by identifier"""
        t = self.base('LVGLButtonWidget', x, y, w, h, {'MAIN': {'DEFAULT': {
            'bg_color': color, 'radius': 8, 'shadow_width': 0}}})
        lbl = self.label(text, 0, 0, None, font, WHITE, expr=expr, align='CENTER')
        lbl['localStyles']['definition']['MAIN']['DEFAULT'].update(align='CENTER')
        t['children'] = [lbl]
        if action:
            self.on_click(t, action)
        return self.hide(t, hidden)

    def checkbox(self, text, var, x, y, font=20):
        t = self.base('LVGLCheckboxWidget', x, y, 200, 32, {'MAIN': {'DEFAULT': {
            'text_color': WHITE, 'text_font': f'MONTSERRAT_{font}'}}})
        t.update(widthUnit='content', heightUnit='content', text=text, textType='literal',
                 checkedStateType='expression', checkedState=var)
        return t

    def dropdown(self, options_var, sel_var, x, y, w, literal_options=None, font=20):
        t = self.base('LVGLDropdownWidget', x, y, w, 44, {'MAIN': {'DEFAULT': {
            'text_font': f'MONTSERRAT_{font}', 'bg_color': BTN, 'text_color': WHITE,
            'border_width': 1, 'border_color': BORDER}}})
        t['heightUnit'] = 'content'
        if literal_options is not None:
            t.update(options='\n'.join(literal_options), optionsType='literal')
        else:
            t.update(options=options_var, optionsType='expression')
        t.update(selected=sel_var, selectedType='expression', direction='bottom')
        return t

    def textarea(self, var, x, y, w, placeholder='', password=False, maxlen=64, accepted=''):
        t = self.base('LVGLTextareaWidget', x, y, w, 44, {'MAIN': {'DEFAULT': {
            'text_font': 'MONTSERRAT_20', 'bg_color': BTN, 'text_color': WHITE,
            'border_width': 1, 'border_color': BORDER}}})
        t.update(text=var, textType='expression', placeholder=placeholder, oneLineMode=True,
                 passwordMode=password, acceptedCharacters=accepted, maxTextLength=maxlen)
        return t

    def slider(self, var, vmin, vmax, x, y, w):
        t = self.base('LVGLSliderWidget', x, y, w, 16, {
            'MAIN': {'DEFAULT': {'bg_color': BTN}},
            'INDICATOR': {'DEFAULT': {'bg_color': ACCENT}},
            'KNOB': {'DEFAULT': {'bg_color': WHITE}}})
        t.update(min=vmin, minType='literal', max=vmax, maxType='literal', mode='NORMAL',
                 value=var, valueType='expression', previewValue=vmax, valueLeft=0,
                 valueLeftType='literal', previewValueLeft=0, enableAnimation=False)
        return t


def _bar(self, var, vmin, vmax, x, y, w, h, color=ACCENT, hidden=None):
    t = self.base('LVGLBarWidget', x, y, w, h, {
        'MAIN': {'DEFAULT': {'bg_color': BTN, 'radius': 4}},
        'INDICATOR': {'DEFAULT': {'bg_color': color, 'radius': 4}}})
    t.update(min=vmin, minType='literal', max=vmax, maxType='literal', mode='NORMAL',
             value=var, valueType='expression', previewValue=(vmin + vmax) // 2,
             valueLeft=0, valueLeftType='literal', previewValueLeft=0, enableAnimation=False)
    return self.hide(t, hidden)


B.bar = _bar


def ident(t, name):
    """EEZ identifier -> objects.<name> in screens.h, for what bindings can't do."""
    t['identifier'] = name
    return t


# ------------------------------------------------------------------ chrome

def nav_button(b, name, text, i, color=SIDEBAR):
    return b.button(text, 5, 8 + i * NAV_STEP, SIDEBAR_W - 10, NAV_H, f'nav_{name.lower()}', color=color, font=16)


def chrome(b, active, title):
    """Sidebar with the page buttons (`active` highlighted), title, status line."""
    btns = [nav_button(b, n, t, i, ACCENT if n == active else SIDEBAR) for i, (n, t, _) in enumerate(MENU_PAGES)]
    return [
        b.panel(0, 0, SIDEBAR_W, 480, btns, bg=SIDEBAR, radius=0, pad=0, border=False),
        b.label(title, CX, 8, None, 24),
        b.label('sys_status', CX + 250, 12, CW - 250, 16, GREY, expr=True, align='RIGHT'),
    ]


# ------------------------------------------------------------------ pages

def page_home(b):
    w2 = (CW - 10) // 2
    return [
        b.panel(CX, TOP, w2, 230, [b.label('home_info', 0, 0, w2 - 26, 16, expr=True, wrap=True)]),
        b.panel(CX + w2 + 10, TOP, w2, 230, [b.label('home_live', 0, 0, w2 - 26, 16, expr=True, wrap=True)]),
        b.panel(CX, TOP + 240, CW, CH - 240, [
            b.label('Services', 0, 0, None, 16, GREY),
            b.label('home_services', 0, 26, CW - 26, 16, expr=True, wrap=True)]),
    ]


def page_obd(b):
    iw = CW - 26
    bw = (iw - 20) // 3
    top_h = 172                 # status + detail (up to 4 lines, full width under the buttons)
    mid_y = TOP + top_h + 10
    bot_y = mid_y + 112 + 10
    return [
        b.panel(CX, TOP, CW, top_h, [
            b.label('conn_status', 0, 0, iw - 310, 20, expr=True, wrap=True),
            b.label('obd_detail', 0, 68, iw, 16, GREY, expr=True, wrap=True),
            # MIL telltale (PID 01, always polled), only while connected; tap = DTC page
            ident(b.button('obd_mil_text', iw - 290, 0, 70, 60, 'nav_readdtc', BTN, font=24, expr=True), 'obd_mil_bt'),
            b.button('conn_button_text', iw - 210, 0, 210, 60, 'conn_button_pressed', ACCENT, expr=True),
        ]),
        b.panel(CX, mid_y, CW, 112, [
            b.label('Live views  (swipe left / right to switch, up / down to come back)', 0, 0, None, 16, GREY),
            b.button('Drive', 0, 28, bw, 58, 'obd_drive', ACCENT, font=24),
            b.button('Race', bw + 10, 28, bw, 58, 'obd_race', ACCENT, font=24),
            b.button('Tune', 2 * (bw + 10), 28, bw, 58, 'obd_tune', ACCENT, font=24),
        ]),
        b.panel(CX, bot_y, CW, TOP + CH - bot_y, [
            b.button('Select parameters', 0, 0, 250, 48, 'nav_params', font=20),
            b.label('obd_speed_text', 270, 0, iw - 270, 16, expr=True),
            b.slider('obd_read_speed', 1, 6, 278, 30, 300),
            b.checkbox('Log parameters to SD (CSV)', 'obd_log_params', 0, 62, 16),
        ]),
    ]


def page_dtc(b):
    rw = 190
    lw = CW - rw - 10
    return [
        b.panel(CX, TOP, lw, CH, [
            b.label('read_dtc_text', 0, 0, lw - 26, 16, expr=True, wrap=True)], scroll=True),
        b.panel(CX + lw + 10, TOP, rw, CH, [
            ident(b.button('Read', 0, 0, rw - 26, 70, None, ACCENT, font=24), 'read_dtc_bt'),
            ident(b.button('Clear', 0, 82, rw - 26, 70, None, DANGER, font=24), 'clear_dtc_bt'),
            ident(b.button('Save to SD', 0, 164, rw - 26, 56, None, font=20), 'save_dtc_bt'),
            b.button('Freeze\nframe', 0, 232, rw - 26, 70, 'nav_freeze', font=20),
        ]),
    ]


def page_freeze(b):
    return [
        ident(b.panel(CX, TOP, CW - 200, CH, scroll=True), 'ff_panel'),
        b.panel(CX + CW - 190, TOP, 190, CH, [
            b.button('Back to\ncodes', 0, 0, 164, 70, 'nav_readdtc', font=20),
        ]),
    ]


def page_params(b):
    return [
        ident(b.panel(CX, TOP, CW - 200, CH, scroll=True, pad=8), 'obd_srv_table'),
        b.panel(CX + CW - 190, TOP, 190, CH, [
            b.label('Only the ticked parameters are polled; unsupported ones are greyed out.',
                    0, 0, 164, 16, GREY, wrap=True),
            b.button('Back to\nOBD', 0, CH - 26 - 70, 164, 70, 'nav_obd', font=20),
        ]),
    ]


def page_can(b):
    bitrates = ['125 kbit/s', '250 kbit/s', '500 kbit/s', '1 Mbit/s']     # order = BITRATES in view_can.c
    lw = 250
    rw = CW - lw - 10
    return [
        b.panel(CX, TOP, lw, CH, [
            b.label('Board CAN bitrate', 0, 0, None, 16, GREY),
            b.dropdown(None, 'can_bitrate_sel', 0, 22, lw - 26, literal_options=bitrates, font=16),
            b.checkbox('Listen only', 'can_listen_only', 0, 76, 16),
            b.button('can_run_text', 0, 112, lw - 26, 50, 'can_start_stop', ACCENT, expr=True),
            b.button('Clear stats', 0, 172, lw - 26, 40, 'can_reset', font=16),
            b.label('Raw log to SD', 0, 228, None, 16, GREY),
            b.checkbox('Log raw CAN', 'can_log_raw', 0, 254, 16),
            b.checkbox('All traffic', 'can_log_all', 0, 284, 16),
            b.label('can_log_info', 0, 318, lw - 26, 12, GREY, expr=True, wrap=True),
        ]),
        b.panel(CX + lw + 10, TOP, rw, 130, [
            b.label('can_stats', 0, 0, rw - 26 - 150, 12, expr=True, wrap=True),
            b.label('Bus load', rw - 26 - 140, 0, None, 12, GREY),
            b.label('can_load_text', rw - 26 - 60, 0, 60, 12, expr=True, align='RIGHT'),
            b.bar('can_load', 0, 100, rw - 26 - 140, 22, 140, 18),
        ]),
        b.panel(CX + lw + 10, TOP + 140, rw, CH - 140, [
            ident(b.label('can_ids', 0, 0, rw - 16, 'MONTSERRAT_12', expr=True), 'can_id_table')],
                scroll=True, pad=8),
    ]


def page_wifi(b):
    return [
        b.panel(CX, TOP, CW, 200, [
            b.button('Scan', 0, 0, 120, 44, 'wifi_scan', ACCENT),
            b.label('wifi_scan_text', 135, 12, 480, 16, GREY, expr=True),
            b.label('Network', 0, 64, None, 16, GREY),
            b.dropdown('wifi_list', 'wifi_sel', 100, 54, 520),
            b.label('Password', 0, 122, None, 16, GREY),
            b.textarea('wifi_pass', 100, 112, 520, 'password (empty = open network)', password=True),
        ]),
        b.panel(CX, TOP + 210, 400, CH - 210, [
            b.label('wifi_status', 0, 0, 374, 16, expr=True, wrap=True)], scroll=True),
        b.panel(CX + 410, TOP + 210, CW - 410, CH - 210, [
            b.button('Connect', 0, 0, 216, 44, 'wifi_connect', ACCENT),
            b.button('Disconnect', 0, 54, 216, 44, 'wifi_disconnect'),
            b.button('Forget', 0, 108, 216, 44, 'wifi_forget', DANGER),
            b.checkbox('Auto connect', 'wifi_autoconnect', 0, 160, 16),
        ]),
    ]


def page_ble(b):
    return [
        b.panel(CX, TOP, CW, 130, [
            b.button('Scan 10 s', 0, 0, 140, 44, 'ble_scan', ACCENT),
            b.label('ble_scan_text', 155, 12, 460, 16, GREY, expr=True),
            b.label('Device', 0, 64, None, 16, GREY),
            b.dropdown('ble_list', 'ble_sel', 100, 54, 520, font=16),
        ]),
        b.panel(CX, TOP + 140, 300, CH - 140, [
            b.label('ble_status', 0, 0, 274, 16, expr=True, wrap=True)], scroll=True),
        b.panel(CX + 310, TOP + 140, 160, CH - 140, [
            b.button('Connect\n+ pair', 0, 0, 134, 70, 'ble_connect', ACCENT, font=16),
            b.button('Disconnect', 0, 80, 134, 50, 'ble_disconnect', font=16),
            b.button('Forget\nbonds', 0, 140, 134, 56, 'ble_forget', DANGER, font=16),
            b.button('Adapter\nconsole', 0, 206, 134, 52, 'nav_ble_console', font=16),
        ]),
        b.panel(CX + 480, TOP + 140, CW - 480, CH - 140, [
            b.label('GATT services', 0, 0, None, 16, GREY),
            b.label('ble_services', 0, 26, CW - 506, 12, expr=True, wrap=True)], scroll=True),
    ]


def page_io(b):
    iw = CW - 26
    w2 = (iw - 20) // 2
    filters = ['Auto', 'Allow all', 'OBD 7E0-7EF', 'Dash DBC IDs', 'Custom']   # order = view_io.c LF_*
    return [
        # car interface: link for OBD and the Dash + receive filter for listening
        # (keyboard covers the bottom 220 px: the hex fields stay above it)
        b.panel(CX, TOP, CW, 150, [
            b.label('Car interface (OBD, Dash)', 0, 0, None, 16, GREY),
            b.dropdown(None, 'obd_link_sel', 0, 22, 220, literal_options=['Board CAN', 'BLE adapter'], font=16),
            b.label('link_status', 236, 22, iw - 236, 12, expr=True, wrap=True),
            b.label('Receive filter', 0, 86, None, 16, GREY),
            b.dropdown(None, 'link_filter_sel', 120, 76, 210, literal_options=filters, font=16),
            b.textarea('link_filter_id', 344, 76, 130, 'ID 7E8', maxlen=3, accepted='0123456789abcdefABCDEF'),
            b.textarea('link_filter_mask', 484, 76, iw - 484, 'mask 7FF', maxlen=3, accepted='0123456789abcdefABCDEF'),
        ]),
        b.panel(CX, TOP + 160, CW, 150, [
            b.label('io_di0_text', 0, 0, w2, 20, expr=True),
            b.label('io_di1_text', w2 + 20, 0, w2, 20, expr=True),
            b.button('io_do0_text', 0, 36, w2, 44, 'io_do0_toggle', ACCENT, expr=True),
            b.button('io_do1_text', w2 + 20, 36, w2, 44, 'io_do1_toggle', ACCENT, expr=True),
            b.label('io_di_info', 0, 90, iw - 190, 12, GREY, expr=True, wrap=True),
            b.button('Reset counters', iw - 170, 90, 170, 32, 'io_reset', font=12),
        ]),
        b.panel(CX, TOP + 320, CW, CH - 320, [
            b.label('io_chip_text', 0, 0, iw, 16, expr=True),
            b.button('Scan I2C', 0, 28, 140, 44, 'i2c_scan', ACCENT, font=16),
            b.label('i2c_text', 155, 36, iw - 155, 16, expr=True, wrap=True),
        ]),
    ]


def page_ble_console(b):
    rw = 190
    hw = (rw - 26 - 10) // 2
    return [
        b.panel(CX, TOP, CW - rw - 10, CH, [
            b.label('elm_log', 0, 0, CW - rw - 36, 16, expr=True, wrap=True)]),
        b.panel(CX + CW - rw, TOP, rw, CH, [
            b.label('elm_info', 0, 0, rw - 26, 12, GREY, expr=True, wrap=True),
            b.button('ATZ', 0, 56, hw, 44, 'elm_atz', ACCENT, font=16),
            b.button('ATI', hw + 10, 56, hw, 44, 'elm_ati', font=16),
            b.button('ATRV', 0, 106, hw, 44, 'elm_atrv', font=16),
            b.button('0100', hw + 10, 106, hw, 44, 'elm_0100', font=16),
            b.textarea('elm_cmd', 0, 160, rw - 26, 'command', maxlen=40),
            b.button('Send', 0, 212, hw, 44, 'elm_send', ACCENT, font=16),
            b.button('Clear', hw + 10, 212, hw, 44, 'elm_clear', font=16),
            b.button('Back to\nBLE', 0, CH - 26 - 60, rw - 26, 60, 'nav_ble', font=16),
        ]),
    ]


DASH_TILES = 9          # 3 x 3, view_dash.c DASH_TILES


def page_dash(b):
    # B2: tile dash; status line + Settings on top
    gap = 10
    gy = TOP + 46
    tw = (CW - 2 * gap) // 3
    th = (CH - 46 - 2 * gap) // 3
    tiles = []
    for i in range(DASH_TILES):
        x = CX + (i % 3) * (tw + gap)
        y = gy + (i // 3) * (th + gap)
        n = i + 1
        tiles.append(ident(b.panel(x, y, tw, th, [
            b.label(f'dash_t{n}_name', 0, 0, tw - 20, 16, GREY, expr=True),
            b.label(f'dash_t{n}_value', 0, 26, tw - 20, 40, expr=True, align='CENTER'),
            b.label(f'dash_t{n}_unit', 0, th - 20 - 22, tw - 20, 16, GREY, expr=True, align='RIGHT'),
        ], pad=10), f'dash_tile{n}'))
    return [
        b.label('dash_status', CX, TOP + 6, CW - 160, 12, GREY, expr=True, wrap=True),
        b.button('Settings', CX + CW - 140, TOP, 140, 36, 'nav_dash_setup', font=16),
    ] + tiles


def page_dash_tile(b):
    # keyboard (screen.c) covers the bottom 220 px: the limit fields stay above it
    iw = CW - 26
    return [
        b.panel(CX, TOP, CW, CH, [
            b.label('dash_edit_title', 0, 10, 100, 20, expr=True),
            b.dropdown('dash_sig_list', 'dash_sig_sel', 110, 0, iw - 110, font=16),
            b.label('dash_edit_info', 0, 52, iw, 12, GREY, expr=True),
            b.label('below', 130, 76, None, 12, GREY),
            b.label('above', 330, 76, None, 12, GREY),
            b.label('Warning', 0, 106, None, 16, '#e0b341'),
            b.textarea('dash_wlo', 130, 96, 180, '-', maxlen=10, accepted='0123456789.-'),
            b.textarea('dash_whi', 330, 96, 180, '-', maxlen=10, accepted='0123456789.-'),
            b.label('Alarm', 0, 158, None, 16, '#ff6060'),
            b.textarea('dash_alo', 130, 148, 180, '-', maxlen=10, accepted='0123456789.-'),
            b.textarea('dash_ahi', 330, 148, 180, '-', maxlen=10, accepted='0123456789.-'),
            b.label('Empty = no limit. The tile turns amber at a warning and red (blinking) at an alarm.',
                    0, 204, iw, 16, GREY, wrap=True),
            b.button('Save', 0, CH - 26 - 50, 164, 50, 'dash_tile_save', ACCENT, font=16),
            b.button('Clear limits', 184, CH - 26 - 50, 164, 50, 'dash_tile_clear', font=16),
            b.button('Back to\nDash', iw - 164, CH - 26 - 50, 164, 50, 'nav_dash', font=16),
        ]),
    ]


def page_dash_signals(b):
    rw = 190
    return [
        b.panel(CX, TOP, CW - rw - 10, CH, [
            ident(b.label('dash_signals', 0, 0, CW - rw - 26, 'MONTSERRAT_12', expr=True), 'dash_signal_table')],
            scroll=True, pad=8),
        b.panel(CX + CW - rw, TOP, rw, CH, [
            b.label('Every signal of the DBC: value, unit, time since it was received.',
                    0, 0, rw - 26, 16, GREY, wrap=True),
            b.button('Back to\nsettings', 0, CH - 26 - 60, rw - 26, 60, 'nav_dash_setup', font=16),
        ]),
    ]


def page_dash_setup(b):
    iw = CW - 26
    return [
        b.panel(CX, TOP, CW, CH, [
            b.label('DBC file (upload on the web page into db/dbc/)', 0, 0, None, 16, GREY),
            b.dropdown('dash_files', 'dash_file_sel', 0, 26, 420, font=16),
            b.label('Base ID: bus ID of the DBC\'s first (lowest) message, decimal or 0x hex;'
                    ' empty or 0 = as in the file. rusEFI: its CAN base setting (default 512 = 0x200).',
                    0, 90, iw, 16, GREY, wrap=True),
            b.textarea('dash_base_text', 0, 140, 200, '0', maxlen=8, accepted='0123456789xXabcdefABCDEF'),
            b.button('Apply', 220, 140, 160, 44, 'dash_apply', ACCENT, font=16),
            b.label('dash_setup_info', 0, 196, iw, 16, expr=True, wrap=True),
            b.label('Tiles: tap a tile on the Dash page to choose its signal and warning / alarm limits.',
                    0, 290, iw - 360, 16, GREY, wrap=True),
            b.button('All signals', iw - 340, CH - 26 - 50, 164, 50, 'nav_dash_signals', font=16),
            b.button('Back to\nDash', iw - 164, CH - 26 - 50, 164, 50, 'nav_dash', font=16),
        ]),
    ]


def page_diag(b):
    iw = CW - 26
    return [
        b.panel(CX, TOP, CW, 110, [
            b.label('Database (db/diag/ on the SD card, upload on the web page)', 0, 0, None, 12, GREY),
            b.dropdown('diag_files', 'diag_file_sel', 0, 20, 330, font=16),
            b.button('diag_scan_text', 344, 20, 170, 44, 'diag_scan', ACCENT, font=16, expr=True),
            b.label('diag_status', 0, 72, iw, 12, GREY, expr=True, wrap=True),
        ]),
        # rows built by view_diag.c (tap = ECU page)
        ident(b.panel(CX, TOP + 120, CW, CH - 120, scroll=True, pad=8), 'diag_ecu_list'),
    ]


def page_diag_ecu(b):
    rw = 190
    bw = rw - 26
    return [
        b.panel(CX, TOP, CW - rw - 10, CH, [
            b.label('diag_ecu_title', 0, 0, CW - rw - 36, 20, expr=True),
            b.label('diag_ecu_text', 0, 34, CW - rw - 36, 16, expr=True, wrap=True)], scroll=True),
        b.panel(CX + CW - rw, TOP, rw, CH, [
            b.button('Identification', 0, 0, bw, 50, 'diag_read_info', ACCENT, font=16),
            b.button('Read DTCs', 0, 60, bw, 50, 'diag_read_dtc', ACCENT, font=16),
            b.button('Clear DTCs', 0, 120, bw, 50, 'diag_clear_dtc', DANGER, font=16),
            b.button('Save to SD', 0, 180, bw, 50, 'diag_save', font=16),
            b.button('Back to\nECU list', 0, CH - 26 - 60, bw, 60, 'nav_diag', font=16),
        ]),
    ]


def page_display(b):
    timeouts = ['Never', '30 s', '1 min', '5 min', '10 min']
    return [
        b.panel(CX, TOP, CW, 204, [
            b.label('disp_brightness_text', 0, 0, None, 20, expr=True),
            b.slider('disp_brightness', 10, 100, 10, 40, CW - 50),
            b.label('Backlight is on/off only on this board: brightness dims the picture (software).',
                    0, 66, CW - 26, 12, GREY, wrap=True),
            b.label('Screen off after', 0, 106, None, 16, GREY),
            b.dropdown(None, 'disp_timeout_sel', 150, 96, 180, literal_options=timeouts),
            b.button('Screen off now', 350, 96, 200, 44, 'screen_off', font=16),
            b.checkbox('Gauge animation (needles move smoothly between readings)', 'disp_gauge_anim', 0, 152, 16),
        ]),
        b.panel(CX, TOP + 214, CW, CH - 214, [
            b.label('sd_text', 0, 0, CW - 26 - 200, 16, expr=True, wrap=True),
            b.button('Remount SD', CW - 26 - 180, 0, 180, 44, 'sd_remount', font=16),
            b.label('sys_version', 0, 110, CW - 26 - 200, 12, GREY, expr=True, wrap=True),
            b.button('Restart', CW - 26 - 180, CH - 214 - 26 - 44, 180, 44, 'restart', DANGER, font=16),
        ]),
    ]


BUILDERS = {
    'home': page_home, 'obd': page_obd, 'readDTC': page_dtc, 'can': page_can, 'wifi': page_wifi,
    'ble': page_ble, 'io': page_io, 'display': page_display,
    'dash': page_dash, 'diag': page_diag, 'ble_console': page_ble_console, 'dash_setup': page_dash_setup, 'dash_signals': page_dash_signals, 'dash_tile': page_dash_tile, 'diag_ecu': page_diag_ecu,
    'select_params': page_params, 'freezFrame': page_freeze,
}


def make_page(tpl, b, name, active, title):
    page = copy.deepcopy(tpl['page'])
    fresh_ids(page)
    page.update(name=name, createAtStart=True, deleteOnScreenUnload=False)
    scr = b.base('LVGLScreenWidget', 0, 0, 800, 480, {'MAIN': {'DEFAULT': {'bg_color': BG}}})
    scr['widgetFlags'] = 'CLICKABLE|PRESS_LOCK|CLICK_FOCUSABLE|GESTURE_BUBBLE|SNAPPABLE'
    if name in ('readDTC', 'freezFrame'):
        ident(scr, {'readDTC': 'read_dtc', 'freezFrame': 'freez_frame'}[name])   # the bridge hooks SCREEN_LOADED
    scr['children'] = chrome(b, active, title) + BUILDERS[name](b)
    page['components'] = [scr]
    return page


def uses_action(proj, name):
    def walk(o):
        if isinstance(o, dict):
            if o.get('action') == name:
                return True
            return any(walk(v) for v in o.values())
        if isinstance(o, list):
            return any(walk(v) for v in o)
        return False
    return walk(proj['userPages'])


def main():
    with open(TEMPLATES, encoding='utf-8') as f:
        tpl = json.load(f)
    with open(PROJECT, encoding='utf-8') as f:
        proj = json.load(f)
    b = B(tpl)

    new = {n: make_page(tpl, b, n, n, t) for n, _, t in MENU_PAGES}
    new.update({n: make_page(tpl, b, n, parent, t) for n, parent, t in SUB_PAGES})
    pages = {p['name']: p for p in proj['userPages'] if p['name'] not in OLD_PAGES}
    pages.update(new)
    missing = [n for n in ORDER if n not in pages]
    if missing:
        raise SystemExit(f'pages missing from the project: {missing}')
    extra = [n for n in pages if n not in ORDER]
    proj['userPages'] = [pages[n] for n in ORDER] + [pages[n] for n in extra]

    gvars = proj['variables']['globalVariables']
    have = {v['name'] for v in gvars}
    for n, (t, d) in VARIABLES.items():
        if n not in have:
            gvars.append({'objID': uid(), 'name': n, 'type': t, 'defaultValue': d, 'persistent': False})

    actions = [a for a in proj['actions'] if a['name'] not in OLD_ACTIONS or uses_action(proj, a['name'])]
    have = {a['name'] for a in actions}
    for a in ACTIONS:
        if a not in have:
            actions.append({'objID': uid(), 'components': [], 'connectionLines': [], 'localVariables': [],
                            'userProperties': [], 'name': a, 'implementationType': 'native'})
    proj['actions'] = actions

    with open(PROJECT, 'w', encoding='utf-8') as f:
        json.dump(proj, f, indent=2, ensure_ascii=False)
    print(f'{os.path.relpath(PROJECT)}: {len(proj["userPages"])} screens '
          f'({", ".join(p["name"] for p in proj["userPages"])}), {len(gvars)} variables, {len(actions)} actions')


if __name__ == '__main__':
    main()
