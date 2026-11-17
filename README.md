# Pokémon Gunk's Pearl Version

A quality-of-life hack of Pokémon Pearl, built on [pret's Diamond and Pearl decompilation](https://github.com/pret/pokediamond)
and ported from Gunk's Platinum. It keeps the game's difficulty and story as they are and takes the grind and
friction out, for long playthroughs.

## Features

- **DexNav.** Dawn (or Lucas) gives it to you on Route 202, after the catching tutorial. Its screen shows each
  area's land and water Pokémon: caught in color with a Poké Ball mark, seen as a silhouette, unseen as a question
  mark. Pick one, then press **R** in the tall grass or a cave, or while surfing for a water Pokémon, to search for
  it. A Pokétch app shows the same area at a glance. Searches roll legal wild encounters, and search levels and
  chains raise the odds of good IVs and shinies. Searches are refused in the Great Marsh.
- **Pokémon follow you**, ported from Following Platinum, with emotes and interactions; the follower returns to
  its Poké Ball while you Surf.
- **21 PC boxes** (18 in the original game): a living dex through Sinnoh with three boxes to spare.
- **Faster play:** an INSTANT text speed (Options), faster HP and EXP bars, auto-run, eggs hatch twice as fast.
- **Quick Run and Quick Ball** in wild battles: press **R** to run, or tap **L** to throw the ball shown under
  "What will X do?". Hold L and press Left or Right to pick another ball; it remembers the last one thrown.
- **Items:** reusable TMs, two registered items (**Y** and **Select**), Start sorts the open Bag pocket, a prompt
  to use another Repel when one runs out, and an Endless Repel toggle.
- **Pokémon:** IV and EV view in the summary (A on the skills page), Exp. All, vitamins up to 252 EVs, and trade
  evolutions without trading (by level, or by using the trade item like an evolution stone). HM moves work from
  the party menu without teaching the move, and every Pokémon Center has a Move Tutor.
- **Info:** a slathered honey tree says about how many hours until Pokémon gather, and the Poké Radar says how
  long the chain is.

Every catch, DexNav finds included, is legal in PKHeX. Save safety comes first: every build passes an emulator
test suite that covers saves, power cycles, crashes during a save, full boxes, Hall of Fame entries and
damaged-save recovery.

## Play it

Releases ship a **BPS patch**, not a ROM. You need your own dump of **Pokémon Pearl (USA) Rev 5**:

```
sha1: 99083bf15ec7c6b81b4ba241ee10abd9e80999ac
```

1. Download the `.bps` file from [Releases](../../releases).
2. Apply it to your ROM with [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/) (in the browser),
   Floating IPS, or `py tools/patch/bps.py apply <pearl.nds> <patch.bps> <out.nds>`.
3. Play the result in any DS emulator or flashcart. The patcher checks your ROM first and refuses the wrong one
   (a Diamond ROM included).

**Start a new game.** Saves from the original Pearl don't load: the hack's save holds more boxes and its own
settings. Saves carry over between releases unless the release notes say otherwise. Back up your save before
updating.

**PokeDaisy users:** the companion app addresses game memory directly. Use a PokeDaisy build that lists this
exact release, or turn the companion off; a mismatched build could crash the game.

## Build it

Follow pret's [`INSTALL.md`](INSTALL.md), then run `make pearl COMPARE=0`. A clean build reproduces the release
ROM, and `tools/patch/bps.py make` makes the patch from it. `make pearl` without `COMPARE=0` still checks the
vanilla ROM's hash, so it fails once the hack's changes are in; that is expected.

## Credits

- [pret](https://github.com/pret/pokediamond) for the Diamond and Pearl decompilation this hack is built on.
- **Following Platinum** by Mikelan98 and collaborators: the follower sprites, data, interactions and logic
  are ported from it.
- The DexNav idea comes from later Pokémon games and fan hacks such as Pokémon Unbound.

Pokémon is © Nintendo, Creatures Inc. and GAME FREAK inc. This is a non-commercial fan project and contains
no ROM data; you need your own legally dumped copy of the game.
