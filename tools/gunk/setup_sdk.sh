#!/bin/sh
# Sets up pokediamond's proprietary-tool slots with free stand-ins (run once, from the repo root, in MSYS2):
#   tools/metroskrew        mid-kid's metroskrew (Metrowerks compilers, no license needed), version 0.1.3
#   tools/mwccarm/<ver>/    wrappers that call metroskrew with -wrap:sdk <ver>
#   tools/bin/makelcf.exe   built from pret's NitroSDK subproject sources (see tools/gunk/README.md)
#   tools/bin/makerom.exe   tools/gunk/makerom.py
#   tools/bin/ntrcomp.exe   nitrogfx LZ10
#   tools/bin/rom_header.template.sbin  first 0x4000 bytes of your own base ROM (BASEROM=path)
set -e
BASEROM=${BASEROM:-baserom.nds}
for v in 1.2/sp2p3 1.2/sp3 2.0/base 2.0/sp1; do
  mkdir -p tools/mwccarm/$v
  for t in mwccarm mwasmarm; do
    printf '#!/bin/sh
D=$(dirname "$0")/../../../metroskrew/bin
exec "$D/%s.exe" -wrap:sdk %s "$@"
' $t $v > tools/mwccarm/$v/$t.exe
  done
  # -nostdlib: the wrapper would otherwise add the SDK's MSL and runtime libraries, which pokediamond builds itself
  printf '#!/bin/sh
D=$(dirname "$0")/../../../metroskrew/bin
exec "$D/mwldarm.exe" -wrap:sdk %s -nostdlib "$@"
' $v > tools/mwccarm/$v/mwldarm.exe
done
touch tools/mwccarm/license.dat
mkdir -p tools/bin
cp tools/gunk/bin/makerom.sh tools/bin/makerom.exe
cp tools/gunk/bin/ntrcomp.sh tools/bin/ntrcomp.exe
head -c 16384 "$BASEROM" > tools/bin/rom_header.template.sbin
echo "SDK stand-ins ready"
cp tools/gunk/lcf/ARM9-TS.lcf.template arm9/
cp tools/gunk/lcf/ARM7-TS.lcf.template arm7/
