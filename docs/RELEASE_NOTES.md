# Gunk's Pearl release notes

## v1.0

The first release. Every feature in the README is in it: DexNav, following Pokémon, 21 PC boxes, faster play,
Quick Run and Quick Ball, the item and Pokémon changes, Move Tutors, and the honey tree and Poké Radar info.

**Patch:** `gunks-pearl-v1.0.bps`, for Pokémon Pearl (USA) Rev 5. The patcher refuses any other ROM.

| | sha1 |
|---|---|
| Source ROM (Pokémon Pearl, USA, Rev 5) | `99083bf15ec7c6b81b4ba241ee10abd9e80999ac` |
| Patched ROM (Gunk's Pearl v1.0) | `317e700e4798b1b4a07426552bdbc6117e79b21a` |
| Patch file | `8e760d07f7552d177ad38930b53d0ee77ae3b24b` |

The ROM header title is `GUNK P` and the game code stays `APAE`. A clean `make pearl COMPARE=0` at this release's
commit reproduces the patched ROM byte for byte.

**Saves:** start a new game. Saves from the original Pearl don't load in the hack.

**PokeDaisy:** the companion doesn't support this hack yet. Turn it off, or use a build that lists this release
(see [pokedaisy.md](pokedaisy.md) for the addresses it needs).

**Tested on the release ROM:**
- It boots, plays the real intro, saves, and continues after a power cycle.
- It contains no test hooks.

**Tested on test builds of the same code:**
- Save cycles, full boxes, damaged-save recovery and power cuts during a save.
- Menus.
- Every gym, the Elite Four, the Champion and the Hall of Fame, each gym badge kept after a save and power cycle.
- Every person and scene the hack changed, and the honey tree and Poké Radar messages.
- Warps into all 477 maps. The 6 that hang also hang in the original game when entered that way, and the
  Jubilife TV elevator only fails because the test teleports straight into it.
- A DexNav search in every area with wild Pokémon, on land and on water, with the Great Marsh refused. Three
  areas can't be reached by the test (Sunyshore's sea, Lake Verity's water, Route 204 North's water).
- PKHeX legality of DexNav catches, including shiny, high search levels, long chains and Unown.
