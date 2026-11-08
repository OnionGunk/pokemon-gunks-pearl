# Building without the proprietary SDK zips

pret's `INSTALL.md` asks for two archives pinned in the pret Discord: the Metrowerks compilers (`mwccarm.zip`)
and Nitro SDK 3.2 (`tools/bin`, the LCF templates). Gunk's Diamond builds a byte-identical retail ROM without
either, using free stand-ins:

| Slot | Stand-in |
|---|---|
| `tools/mwccarm/<ver>/mwccarm.exe`, `mwldarm.exe`, `mwasmarm.exe` | Wrappers for [metroskrew](https://github.com/mid-kid/metroskrew) 0.1.3 (`-wrap:sdk <ver>`). Unpack metroskrew's Windows release into `tools/metroskrew`. The linker wrapper adds `-nostdlib`, because pokediamond builds MSL and the runtime itself. |
| `tools/bin/makelcf.exe` | Built from the makelcf sources in pret's `NitroSDK` meson subproject (as used by pokeheartgold and pokeplatinum): flex and bison, then `gcc -DSDK_WIN32`. |
| `arm9/ARM9-TS.lcf.template` | `tools/gunk/lcf/`: pret's NitroSDK 4.2 template with SDK 3.2's `crt0.o` and `* (.version)` lines. |
| `arm7/ARM7-TS.lcf.template` | `tools/gunk/lcf/`: written for this repo. SDK 3.2's ARM7 layout packs sections at 4-byte alignment. |
| `tools/bin/makerom.exe` | `tools/gunk/makerom.py`: lays the ROM out as makerom does for `rom.rsf`. |
| `tools/bin/ntrcomp.exe` | nitrogfx LZ10 compression (`-search 2`; `-A4` pads to 4 bytes). |
| `tools/bin/rom_header.template.sbin` | The first 0x4000 bytes of your own base ROM. |

## Setup (MSYS2)

```
BASEROM=/path/to/diamond.nds sh tools/gunk/setup_sdk.sh
make NOWINE=1 MWASMARM_PATCHER=true
```

`MWASMARM_PATCHER=true` skips pret's assembler patcher: it recognises only the original executables, and
metroskrew's assembler doesn't need the fix.
