#!/bin/sh
# PC test of the DBC parser / decoder (components/dbc). Usage: tools/test/test_dbc.sh
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${TMPDIR:-/tmp}/test_dbc
gcc -std=gnu11 -Wall -Wextra -Werror -I"$root/components/dbc" -o "$out" \
    "$root/tools/test/test_dbc.c" "$root/components/dbc/dbc.c" -lm
"$out"
