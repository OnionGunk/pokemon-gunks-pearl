# PokeDaisy address table

PokeDaisy's live companion reads game memory at fixed addresses. The hack moves the game's RAM globals, and every
rebuild can move them again. A PokeDaisy config for this hack therefore needs one entry per release, picked by the
release's ARM9 CRC. That CRC is PokeDaisy's `pk_nds_arm9_crc`: CRC-32 over the ARM9 binary past its 0x800-byte
secure area, continued over the ARM9 overlay table.

Until PokeDaisy has such an entry, turn the companion off for this hack.

Each release's addresses come from its build's `arm9/build/diamond.us/arm9.elf.xMAP`.

## v1.0

| | Pokémon Diamond (USA) | Gunk's Diamond v1.0 |
|---|---|---|
| ROM sha1 | `a46233d8…41e` | `6ae02bd5…9c7` |
| Header title / game code | `POKEMON D` / `ADAE` | `GUNK D` / `ADAE` |
| ARM9 CRC (`pk_nds_arm9_crc`) | `D956DDF1` | `7EBC92B1` |
| `sSaveDataPtr` (the SaveData pointer) | `021C59C8` | `021D2728` |
| `gSystem` | `021C48B8` | `021D1618` |
| `sFieldSystem` (the running FieldSystem; hack only) | | `021E4234` |
| `sSearch` (the DexNav search state; hack only) | | `021E424C` |

The save data has more PC boxes and new entries for the hack's settings and DexNav records. Read the save layout
from this release's `include/save.h`, `arm9/src/save_arrays.c` and `arm9/src/gunk_save.c`, not from retail Diamond's.
