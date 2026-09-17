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

echo
echo "== compass =="
# Compiles against test/stub/pebble.h rather than the SDK: the orientation
# fallbacks are pure logic, and the footer label is all a pilot has to tell
# the three radar modes apart.
"$CC" -O2 -I test/stub -I src/c -o "$OUT/compass" test/test_compass.c src/c/compass.c -lm
"$OUT/compass"

echo
echo "== format =="
"$CC" -O2 -I test/stub -I src/c -o "$OUT/fmt" test/test_fmt.c \
  src/c/fmt.c src/c/geo.c src/c/geodesy.c src/c/trig.c -lm
"$OUT/fmt"

echo
echo "== phone side =="
# The phone decides what the watch is told: which altitude a target is
# compared against, what happens to a route too long to fit, and which
# messages may be thrown away when the link backs up.
NODE="${NODE:-$(command -v node || true)}"
if [ -n "$NODE" ]; then
  "$NODE" test/test_js.js
else
  echo "  skipped: node not on PATH"
fi
