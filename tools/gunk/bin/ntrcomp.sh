#!/bin/sh
# Gunk's stand-in for the Nitro SDK's ntrcomp: LZ10 via nitrogfx (-l2 = min distance 2, -A4 = pad to 4)
pad=-nopad; out=; in=
while [ $# -gt 0 ]; do case "$1" in -l2|-s) ;; -A4) pad= ;; -o) out=$2; shift ;; *) in=$1 ;; esac; shift; done
D=$(dirname "$0")/..
exec "$D/nitrogfx/nitrogfx" "$in" "$out" -search 2 $pad
