# dpscript: field-script disassembler/assembler for Pokemon Diamond/Pearl

These tools disassemble, reassemble and patch the field-script files from the pret/pokediamond
decomp (`files/fielddata/script/scr_seq_release/narc_XXXX.bin`, 1051 members). They are pure
Python 3 and only read the decomp tree.

| file | purpose |
|---|---|
| `gen_cmdsizes.py` | Works out each opcode's argument layout from the C/asm sources and writes `cmdsizes.py`. It holds the documented `OVERRIDES` table. |
| `cmdsizes.py` | Generated. `CMDS[opcode] = {name, args, src, conditional, notes[, override, derived]}` and `TERMINATORS`. |
| `dpscript.py` | `disasm()`, `asm()`, the `Script` patch API, and a CLI. |
| `roundtrip.py` | Disassembles and reassembles all 1051 files, checks they come back byte-identical, and reports coverage and desyncs. |
| `test_patch.py` | Self-test for the patch API. It shifts code in every script file, reassembles, and re-disassembles. |

## Usage

```sh
python gen_cmdsizes.py [C:\Projects\pokemon-pearl-gunk]   # regenerate cmdsizes.py
python roundtrip.py [-v] [--dump OUTDIR]                  # validate everything (-v lists details)
python test_patch.py                                      # patch-API self-test
python dpscript.py disasm narc_0002.bin -o narc_0002.s
python dpscript.py asm narc_0002.s -o narc_0002.bin
```

From Python:

```python
from dpscript import disasm, asm, Script
text = disasm(r"...\narc_0002.bin")      # path or bytes; kind auto-detected from narc index
data = asm(text)

s = Script.load(r"...\narc_0002.bin")
s.commands(1)                                       # list the commands of script 1
sid = s.add_script("LockAllEvents\nFacePlayer\nReleaseAllEvents\nEnd")   # new entry, returns its id
s.add_entry("L_0102")                               # make an existing label a new script id
s.replace_command(1, 3, "SetFlag 0x123")            # 4th command (0-based n=3) after script 1's label
s.insert_before("L_00A0", 0, "Nop")                 # raw insertion (n=0: right after the label)
lab = s.insert_call(1, 0, "SetVar 0x4000, 1")       # put "Call P_xxx" before cmd 0, append block + Return
lab = s.insert_jump(1, 5, "...\nEnd")               # replace cmd 5 with "GoTo P_xxx", append block
mv  = s.add_movement([(0x0C, 1), (0x0D, 2)])        # appends a 4-aligned movement block (0xFE added)
s.insert_before(sid, 0, "Unk005E 0xFF, %s" % mv)    # 0x005E = ApplyMovement(obj, movement)
open(out, "wb").write(s.to_bytes())
```

`where` arguments take a label name or a script id (int, 1-based). Every jump, call and
movement pointer is symbolic, so any insertion recomputes all the relative offsets. New blocks
go at the end of the file, before the trailing `.align`.

## Text format

```
.script L_0012 ; 1        header entry (s32 offset relative to the end of the field); id = order
.endscripts               u16 0xFD13 terminator   (".endscripts noterm" = file has none)
L_0012: ; script 1
    PlayFanfare 0x5DC     command = ScrCmd name without the "ScrCmd_" prefix (prefix also accepted;
    GoToIf 1, L_0050       Cmd_XXXX = raw opcode); r/m arguments take labels
    .align 4              zero padding to a 4-byte boundary (always emitted before movement blocks)
M_0188:
    .mov 0x0C, 1          movement step: u16 cmd, u16 count; 0xFE ends the block
    .byte / .hword / .word / .rel <label>    raw data
```

Labels: `L_` is code, `M_` is movement, `D_` is unclassified data. A label comment of
`unreferenced (decoded speculatively)` marks a block that nothing jumps to (see below).

Level-script files (members 465-976; `map_header.c` gives each map a `scripts_bank` in
0-464/977-1050 and a `level_scripts_bank` in 465-976) use their own directives:

```
.levelscripts
    .ls 3, 0x2328         record: u8 type (2..4), u32 script id
    .ls_table LT_000B     record: u8 type 1, s32 relative offset to a table
    .ls_end               u8 0
LT_000B:
    .lst 0x40D5, 6, 0xA   u16 var, u16 value, u16 script  (run script when var == value)
    .lst_end              u16 0
```

## Format notes

- **Header.** The file starts with a list of u32 offsets. Each is relative to the end of its own
  u32 (`ScriptRunByIndex`: `ptr = &hdr[i] + 4 + hdr[i]`). The list ends with u16 `0xFD13`. Three
  files (0096, 0158, 0356) have no terminator, so the list just runs up to the first
  script. Member 1050 is empty.
- **Commands.** A command is a u16 opcode (index into `gScriptCmdTable`, 721 entries) followed by
  its arguments, all little-endian and unaligned. Argument codes in `cmdsizes.py`:
  `b` u8, `h` u16, `v` u16 var-or-value (`ScriptGetVar`), `p` u16 var id (`ScriptGetVarPointer`),
  `w` u32, `r` s32 code offset, `m` s32 movement offset. Both offsets are relative to the end
  of the 4-byte field.
- **Jumps.** GoTo 0x16, ObjectGoTo 0x17, BgGoTo 0x18, DirectionGoTo 0x19, Call 0x1A, GoToIf 0x1C,
  CallIf 0x1D and Unk00F1 (trainer-flag conditional jump) all take `r`. Only End 0x02,
  GoTo 0x16 and Return 0x1B end straight-line flow.
- **Movement** (Unk005E = ApplyMovement, `v` object plus `m` pointer). A movement block is a
  list of u16 cmd and u16 count pairs ending in cmd `0xFE`. Every movement block in the ROM is
  4-aligned and padded with zeros. The disassembler emits `.align 4` so the padding survives
  code shifts.
- **Unreferenced blocks.** The walker starts from the entry points and follows jumps, calls and
  movement pointers. Anything left over gets a speculative decode: first as a 4-aligned movement
  block, then as code. Only a clean decode is accepted, meaning no bad opcode, no overlap, and
  targets that land on valid items. Pass `speculate=False` to `disasm`/`Script.load` to keep
  those bytes as raw `.byte`. Both modes round-trip identically. These blocks never run, so how
  they are classified only matters if you edit them.

## How the size table is derived (`gen_cmdsizes.py`)

- **C (`arm9/src/scrcmd*.c`).** The generator strips comments and keeps the C branch of
  `#ifdef NONMATCHING`. It then walks each `ScrCmd_*` body in source order and records
  `ScriptReadByte/Halfword/Word` and `ScriptGetVar/GetVarPointer`. It inlines any function that
  is passed a bare `ctx` and reads script bytes itself, including `SetupNativeScript` callbacks.
  A `ScriptReadWord` result that gets added to `scriptPtr` becomes `r`. A read inside
  `if/else/switch/for/while/?:` is flagged as conditional and needs an override.
- **asm (`arm9/asm/scrcmd_{5,9,10,12}.s`, 29 commands).** The generator counts
  `bl ScriptReadHalfword/Word`. The next `bl FieldSystem_VarGet`/`GetVarPointer` decides `v` or
  `p`. Inlined byte reads are detected as `str rX,[rCtx,#0x8]` where `rX = ptr + 1`. A read that
  comes after a conditional branch or a local label is flagged as conditional.
- No overlay code reads script bytes. Only the scrcmd sources call the readers.

## Irregular commands (`OVERRIDES` in gen_cmdsizes.py)

A *switch* layout reads a selector first. The selector's value then decides which arguments
follow. The decoder and the assembler both handle this, and the assembler checks the
argument count for the selector you wrote.

| opcode | name | layout | reason |
|---|---|---|---|
| 0x005E | Unk005E (ApplyMovement) | `v m` | The u32 points to movement data, not code |
| 0x01CF/0x01D0/0x01D1 | GetSetStrength/Flash/Defog | `b` sel; 2 -> `p` | Only "get" (2) reads a result var |
| 0x021D | UnionGroup | `h` sel; 0,1 -> `v p`; 2,3 -> `v v`; 4 -> `p`; 5 -> `v`; 6 -> none | Sub-command switch |
| 0x0235 | Unk0235 (asm) | `h` sel; 0 `p`, 1 `h p p`, 2 none, 3 `v p p`, 4 `p p`, 5 `v v p`, 6 `p` | Jump table |
| 0x0237 | Unk0237 (asm) | `h` sel; 0 `v p p`, 1 `v v v` | |
| 0x023E | Unk023E (asm) | `h` sel; 1,2,3 `p`; 5,6 `p p`; 0,4,7,8 none | Jump table |
| 0x027C | Unk027C (asm, Safari) | `h` sel; 0..2 `v` | |
| 0x0289 | Unk0289 (poffin) | `p b b b b b b` | 5 bytes read in a for-loop, then 1 more |

The original scripts use every switch case except 0x0235 case 5, 0x023E case 2 and
GetSet\* case 0. Their layouts come only from the source.

## Validation results (roundtrip.py)

- 1051/1051 files round-trip byte-identical (538 script, 512 level-script, 1 empty), with and
  without speculation.
- 0 walker warnings: no unknown opcodes, overlaps, out-of-range targets, or code running into
  data.
- Script-file bytes (206,648 total):
  - header: 14,742
  - commands: 174,144
  - movement: 16,892
  - unclassified: 870, of which 869 are zero alignment padding and 1 is a stray `0x01` in
    narc_0362
  - Of the command and movement bytes, 3,982 come from speculatively decoded unreferenced blocks.
- 607 of 721 opcodes appear in reachable code, so their sizes are confirmed by real data. The
  other 114 are never used in the ROM and their sizes come only from the source. Two of those
  are asm-derived (0x0297 `p`, 0x0298 `v`).
- `test_patch.py` passes on all 445 non-trivial script files. It inserts a Nop at script 1 so
  every later offset shifts, then runs a full add_script/insert_call/add_movement/replace
  session, re-disassembles, and finds the same structure.

## Caveats

- Many opcode names are still `UnkXXXX`, as in the decomp. The tool uses whatever names
  `gScriptCmdTable` has, so it picks up renames when you rerun `gen_cmdsizes.py`.
- Level-script record types 2..4 are known to carry a script id. Their trigger meanings have not
  been confirmed from the code.
- Script ids beyond the local header, such as 2000+ common scripts and 9000+ ids in level
  scripts, are resolved by game code to other narc members. dpscript keeps them as plain
  numbers.
