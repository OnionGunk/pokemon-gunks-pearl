#!/bin/sh
# Builds tools/bin/makelcf.exe (run from the repo root, in MSYS2, with gcc, flex and bison).
# Sources: the makelcf in pret's NitroSDK meson subproject (as pokeheartgold/pokeplatinum fetch it); set NITROSDK to
# that checkout. Our makelcf.h replaces its own: SDK 3.2's default IRQ stack is 0x400 (4.x: 0x800), which Diamond needs.
set -e
S=${NITROSDK:-/c/Projects/pokemon-heartgold-gunk/subprojects/NitroSDK-4.2.30001}
ROOT=$(pwd)
W=$(mktemp -d)
cp "$S"/tools/makelcf/* "$W"/
cp "$ROOT/tools/gunk/makelcf/makelcf.h" "$W"/
cd "$W"
for b in spec tlcf; do flex --nowarn -P${b}_yy -o$b.yy.c $b.l; bison -d -p ${b}_yy -o $b.tab.c $b.y; done
gcc -O1 -DSDK_WIN32 -I. -I"$S/include" -I"$S/include/nitro" -o makelcf.exe container.c createlcf.c defval.c makelcf.c misc.c spec.yy.c tlcf.yy.c spec.tab.c tlcf.tab.c
mkdir -p "$ROOT/tools/bin"
cp makelcf.exe "$ROOT/tools/bin/"
echo "makelcf built"
