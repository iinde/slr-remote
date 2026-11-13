#!/bin/sh
# Builds the PC screen renderer and writes one 128x64 PGM per screen.
# Usage: build.sh <U8g2 library folder> <work dir>
#   e.g. build.sh ../../../firmware/.pio/libdeps/heltec_v4/U8g2 /tmp/slr-oled
# (PlatformIO downloads U8g2 into .pio/libdeps the first time the firmware is built.)
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
FW="$HERE/../../../firmware"
U8="$1"; WORK="$2"
mkdir -p "$WORK/obj" "$WORK/pgm"
python3 "$HERE/extract_fonts.py" "$U8/src/clib/u8g2_fonts.c" "$FW/src/ui.cpp" "$WORK/fonts_used.c"
for f in "$U8"/src/clib/*.c; do
  case "$f" in *u8g2_fonts.c|*u8x8_fonts.c) continue;; esac
  o="$WORK/obj/$(basename "$f" .c).o"
  [ -f "$o" ] || cc -c -O1 -w -I"$U8/src" "$f" -o "$o"
done
cc -c -O1 -w -I"$U8/src" "$WORK/fonts_used.c" -o "$WORK/obj/fonts_used.o"
c++ -std=c++17 -O1 -w -I"$HERE/stubs" -I"$U8/src" -I"$FW/src" -I"$FW/include" -I"$FW/lib/core/src" \
  "$FW/src/ui.cpp" "$HERE/render_main.cpp" "$WORK"/obj/*.o -Wl,-dead_strip -o "$WORK/render_oled"
"$WORK/render_oled" "$WORK/pgm"
ls "$WORK/pgm"
