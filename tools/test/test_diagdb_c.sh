#!/bin/sh
# PC test of the diagnostic database parser (components/diagdb).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
out=${TMPDIR:-/tmp}/test_diagdb_c
gcc -std=gnu11 -Wall -Wextra -Werror -I"$root/components/diagdb" -o "$out" \
    "$root/tools/test/test_diagdb_c.c" "$root/components/diagdb/diagdb.c"
"$out" "$root/sd/db/diag/generic.txt"
