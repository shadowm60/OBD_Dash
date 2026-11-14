#!/usr/bin/env python3
"""Build step (components/hmi/CMakeLists.txt): turn the EEZ generated headers
into name tables, so the firmware can refer to UI variables and screens by name:

    vars.h     FlowGlobalVariables enum  -> eez_var_table    (name, index)
    screens.h  ScreensEnum               -> eez_screen_table (name, screen id)

    gen_eez_var_table.py <vars.h> <screens.h> <out.c>
"""
import re
import sys


def table(ctype, name, entries):
    lines = [f'const {ctype} {name}[] = {{']
    for n, idx in entries:
        lines.append(f'    {{ "{n}", {idx} }},')
    if not entries:
        lines.append('    { "", 0 },')
    lines += ['};', f'const unsigned {name}_len = {len(entries)};', '']
    return lines


def main(vars_h, screens_h, dst):
    with open(vars_h, encoding='utf-8') as f:
        variables = re.findall(r'\bFLOW_GLOBAL_VARIABLE_(\w+)\s*=\s*(\d+)', f.read())
    with open(screens_h, encoding='utf-8') as f:
        # _SCREEN_ID_FIRST / _LAST are markers, not screens
        screens = re.findall(r'(?<!_)\bSCREEN_ID_(\w+)\s*=\s*(\d+)', f.read())
    lines = [
        '/* Generated from vars.h + screens.h by tools/gen_eez_var_table.py - do not edit. */',
        '#include "eez_var_table.h"',
        '',
    ]
    lines += table('eez_var_entry_t', 'eez_var_table', variables)
    lines += table('eez_var_entry_t', 'eez_screen_table', screens)
    out = '\n'.join(lines)
    try:
        with open(dst, encoding='utf-8') as f:
            if f.read() == out:
                return 0    # unchanged: don't touch the timestamp
    except OSError:
        pass
    with open(dst, 'w', encoding='utf-8') as f:
        f.write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3]))
