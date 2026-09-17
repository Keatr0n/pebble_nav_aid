#!/usr/bin/env bash
# Host-side tests for the navigation maths.
#
# The watch has no libm trig (see src/c/trig.h for why), so these routines are
# ours and have to be checked against a double-precision reference. They decide
# what distance and bearing a pilot is shown, which makes them the part of this
# app least able to afford being quietly wrong.
set -euo pipefail
cd "$(dirname "$0")/.."

CC="${CC:-$(command -v clang || command -v gcc || command -v cc)}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "== trig =="
"$CC" -O2 -I src/c -o "$OUT/trig" test/test_trig.c src/c/trig.c -lm
"$OUT/trig"

echo
echo "== geodesy =="
"$CC" -O2 -I src/c -o "$OUT/geodesy" test/test_geodesy.c src/c/geodesy.c src/c/trig.c -lm
"$OUT/geodesy"
