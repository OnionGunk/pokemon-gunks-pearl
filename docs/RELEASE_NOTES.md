# Gunk's Diamond release notes

## v1.0

The first release. Every feature in the README is in it: DexNav, following Pokémon, 21 PC boxes, faster play,
Quick Run and Quick Ball, the item and Pokémon changes, Move Tutors, and the honey tree and Poké Radar info.

**Patch:** `gunks-diamond-v1.0.bps`, for Pokémon Diamond (USA). The patcher refuses any other ROM.

| | sha1 |
|---|---|
| Source ROM (Pokémon Diamond, USA) | `a46233d8b79a69ea87aa295a0efad5237d02841e` |
| Patched ROM (Gunk's Diamond v1.0) | `6ae02bd53fe7421344a7127ec95804eca72229c7` |
| Patch file | `22f5e82bf0787a0d3d05d465592efbee95aa70eb` |

The ROM header title is `GUNK D` and the game code stays `ADAE`. A clean `make COMPARE=0` at this release's
commit reproduces the patched ROM byte for byte.

**Saves:** start a new game. Saves from the original Diamond don't load in the hack.

**PokeDaisy:** the companion doesn't support this hack yet. Turn it off, or use a build that lists this release
(see [pokedaisy.md](pokedaisy.md) for the addresses it needs).

**Tested on the release ROM:**
- It boots, plays the real intro, saves, and continues after a power cycle.
- It contains no test hooks.

**Tested on test builds of the same code:**
- Save cycles, full boxes, damaged-save recovery and crashes during a save.
- Menus.
- Every gym, the Elite Four, the Champion and the Hall of Fame.
- Every person and scene the hack changed.
- Warps into all 477 maps. The 8 that hang also hang in the original game when entered that way, and the
  Jubilife TV elevator only fails because the test teleports straight into it.
- A DexNav search in every area with wild Pokémon, on land and on water, with the Great Marsh refused.
- PKHeX legality of DexNav catches, including shiny, Surf and Unown.
