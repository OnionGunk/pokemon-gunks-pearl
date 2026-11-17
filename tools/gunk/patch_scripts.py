#!/usr/bin/env python3
"""Apply Gunk's field-script changes to the binary scripts in files/fielddata/script/scr_seq_release.

pokediamond keeps field scripts only as binaries, so Gunk's changes are made with tools/dpscript (a verified
disassembler/assembler) and recorded here. Run from the repo root; it is idempotent (a file that already has the
changes is left alone):

    python3 tools/gunk/patch_scripts.py

Common scripts (narc_0205, ids 2000+, messages in msg bank 199):
- 2032 (script 33), "Repel's effect wore off": if the last Repel used is still in the Bag (and Endless Repel is
  off), asks whether to use another (script commands 721/722, messages 129/130).
- 2050 (script 51): Endless Repel switched on/off (command 723, messages 131/132).
- 2051 (script 52): Exp. All switched on/off (command 724, messages 133/134).

Route 202 (narc_0393, messages in msg bank 416), Dawn/Lucas's catching tutorial: after the five Poke Balls they
also give the DexNav (item 465) and say how it works (messages 14 and 15). Before the first gym, as in Platinum.
Run one part alone with `python3 tools/gunk/patch_scripts.py route202`.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'dpscript'))
import dpscript  # noqa: E402

SCRIPTS = os.path.join(ROOT, 'files', 'fielddata', 'script', 'scr_seq_release')

CMD_CHECK_REPEL_AGAIN = 721
CMD_USE_REPEL_AGAIN = 722
CMD_TOGGLE_ENDLESS_REPEL = 723
CMD_TOGGLE_EXP_ALL = 724

REPEL_WORE_OFF_OLD = """L_12F4: ; script 33
    PlayFanfare 0x5DC
    LockAllEvents
    Unk002C 0x4F
    WaitButtonAB
    CloseMessageBox
    ReleaseAllEvents
    End
"""

REPEL_WORE_OFF_NEW = f"""L_12F4: ; script 33
    PlayFanfare 0x5DC
    LockAllEvents
    Gunk_CheckRepelAgain 0x800C
    CompareVarToValue 0x800C, 1
    GoToIf 1, L_GUNK_REPEL_ASK
    Unk002C 0x4F
    WaitButtonAB
    CloseMessageBox
    ReleaseAllEvents
    End

L_GUNK_REPEL_ASK:
    Unk002C 129
    YesNoMenu 0x800C
    CompareVarToValue 0x800C, 0
    GoToIf 1, L_GUNK_REPEL_USE
    CloseMessageBox
    ReleaseAllEvents
    End

L_GUNK_REPEL_USE:
    Gunk_UseRepelAgain
    Unk002C 130
    WaitButtonAB
    CloseMessageBox
    ReleaseAllEvents
    End
"""

TOGGLES = f"""
L_GUNK_ENDLESS_REPEL:
    PlayFanfare 0x5DC
    LockAllEvents
    Gunk_ToggleEndlessRepel 0x800C
    CompareVarToValue 0x800C, 1
    GoToIf 1, L_GUNK_ENDLESS_REPEL_ON
    Unk002C 132
    GoTo L_GUNK_TOGGLE_END

L_GUNK_ENDLESS_REPEL_ON:
    Unk002C 131
    GoTo L_GUNK_TOGGLE_END

L_GUNK_EXP_ALL:
    PlayFanfare 0x5DC
    LockAllEvents
    Gunk_ToggleExpAll 0x800C
    CompareVarToValue 0x800C, 1
    GoToIf 1, L_GUNK_EXP_ALL_ON
    Unk002C 134
    GoTo L_GUNK_TOGGLE_END

L_GUNK_EXP_ALL_ON:
    Unk002C 133

L_GUNK_TOGGLE_END:
    WaitButtonAB
    CloseMessageBox
    ReleaseAllEvents
    End
"""


def patch_common():
    path = os.path.join(SCRIPTS, 'narc_0205.bin')
    text = dpscript.disasm(open(path, 'rb').read())
    if 'Gunk_CheckRepelAgain' in text:
        print('narc_0205: already patched')
        return
    assert text.count(REPEL_WORE_OFF_OLD) == 1, 'unexpected script 33'
    text = text.replace(REPEL_WORE_OFF_OLD, REPEL_WORE_OFF_NEW)
    header_end = '.endscripts'
    assert text.count(header_end) == 1
    text = text.replace(header_end, '.script L_GUNK_ENDLESS_REPEL ; 51\n.script L_GUNK_EXP_ALL ; 52\n' + header_end)
    text = text.rstrip() + '\n' + TOGGLES
    data = dpscript.asm(text)
    check = dpscript.disasm(data)
    assert 'Unk002C 0x81' in check and 'script 52' in check, 'reassembled scripts lost the changes'
    open(path, 'wb').write(data)
    print('narc_0205: patched (%d bytes)' % len(data))


# ---- Move Tutor in every Pokemon Center (common script 2052 = script 53, messages 135-146) ----
# Remembering is the Pastoria Move Reminder's flow without the Heart Scale; forgetting is the Canalave Move Deleter's.

TUTOR = """
L_GUNK_TUTOR:
    PlayFanfare 0x5DC
    LockAllEvents
    FacePlayer
    Unk002C 135
    YesNoMenu 0x800C
    CompareVarToValue 0x800C, 0
    GoToIf 1, L_GUNK_TUTOR_REMEMBER
    Unk002C 136
    YesNoMenu 0x800C
    CompareVarToValue 0x800C, 0
    GoToIf 1, L_GUNK_TUTOR_FORGET
    GoTo L_GUNK_TUTOR_BYE

L_GUNK_TUTOR_REMEMBER:
    Unk002C 137
    CloseMessageBox
    FadeScreen 6, 1, 0, 0
    WaitFadeScreen
    ChoosePokemonMenu
    GetSelectedPartySlot 0x8005
    RestoreOverworld
    FadeScreen 6, 1, 1, 0
    WaitFadeScreen
    CompareVarToValue 0x8005, 0xFF
    GoToIf 1, L_GUNK_TUTOR_BYE
    GetPartyMonSpecies 0x8005, 0x800C
    CompareVarToValue 0x800C, 0
    GoToIf 1, L_GUNK_TUTOR_EGG
    Unk021F 0x800C, 0x8005
    CompareVarToValue 0x800C, 0
    GoToIf 1, L_GUNK_TUTOR_NOTHING
    Unk002C 138
    CloseMessageBox
    FadeScreen 6, 1, 0, 0
    WaitFadeScreen
    Unk0221 0x8005
    Unk0223 0x800C
    RestoreOverworld
    FadeScreen 6, 1, 1, 0
    WaitFadeScreen
    GoTo L_GUNK_TUTOR_BYE

L_GUNK_TUTOR_FORGET:
    Unk002C 141
    CloseMessageBox
    FadeScreen 6, 1, 0, 0
    WaitFadeScreen
    ChoosePokemonMenu
    GetSelectedPartySlot 0x8002
    RestoreOverworld
    FadeScreen 6, 1, 1, 0
    WaitFadeScreen
    CompareVarToValue 0x8002, 0xFF
    GoToIf 1, L_GUNK_TUTOR_BYE
    GetPartyMonSpecies 0x8002, 0x8001
    CompareVarToValue 0x8001, 0
    GoToIf 1, L_GUNK_TUTOR_EGG
    CountPartyMonMoves 0x800C, 0x8002
    CompareVarToValue 0x800C, 1
    GoToIf 1, L_GUNK_TUTOR_ONE_MOVE
    Unk002C 142
    CloseMessageBox
    FadeScreen 6, 1, 0, 0
    WaitFadeScreen
    Unk01C6 0x8002
    Unk01C7 0x8001
    RestoreOverworld
    FadeScreen 6, 1, 1, 0
    WaitFadeScreen
    CompareVarToValue 0x8001, 0xFF
    GoToIf 1, L_GUNK_TUTOR_BYE
    GetPokemonMoveName 0, 0x8002, 0x8001
    Unk002C 144
    YesNoMenu 0x800C
    CompareVarToValue 0x800C, 0
    GoToIf 0, L_GUNK_TUTOR_BYE
    ForgetPartyMonMove 0x8002, 0x8001
    Unk002C 145
    PlaySound 0x48D
    PlaySoundWait
    GoTo L_GUNK_TUTOR_BYE

L_GUNK_TUTOR_EGG:
    Unk002C 140
    GoTo L_GUNK_TUTOR_BYE

L_GUNK_TUTOR_NOTHING:
    Unk002C 139
    GoTo L_GUNK_TUTOR_BYE

L_GUNK_TUTOR_ONE_MOVE:
    Unk002C 143

L_GUNK_TUTOR_BYE:
    Unk002C 146
    WaitButton
    CloseMessageBox
    ReleaseAllEvents
    End
"""

SCRIPT_MOVE_TUTOR = 2052

# Pokemon Center 1F maps (include/constants/maps.h) and where the tutor stands (Gunk's Platinum: x 1, z 6)
POKECENTER_1F_MAPS = [
    'MAP_JUBLIFE_POKECENTER_1F', 'MAP_CANALAVE_POKECENTER_1F', 'MAP_OREBURGH_POKECENTER_1F',
    'MAP_ETERNA_POKECENTER_1F', 'MAP_HEARTHOME_POKECENTER_1F', 'MAP_PASTORIA_POKECENTER_1F',
    'MAP_VEILSTONE_POKECENTER_1F', 'MAP_SUNYSHORE_POKECENTER_1F', 'MAP_SNOWPOINT_POKECENTER_1F',
    'MAP_POKEMON_LEAGUE_POKECENTER_1F', 'MAP_FIGHT_AREA_POKECENTER_1F', 'MAP_SANDGEM_POKECENTER_1F',
    'MAP_FLOAROMA_POKECENTER_1F', 'MAP_SOLACEON_POKECENTER_1F', 'MAP_CELESTIC_POKECENTER_1F',
    'MAP_SURVIVAL_AREA_POKECENTER_1F', 'MAP_RESORT_AREA_POKECENTER_1F',
]
TUTOR_X, TUTOR_Z = 1, 6
TUTOR_POSITION = {'MAP_FLOAROMA_POKECENTER_1F': (1, 7)}  # (1, 6) is taken there, as in Platinum
SPRITE_GENTLEMAN = 36
MOVEMENT_STILL = 0
FACING_EAST = 3


def patch_tutor_script():
    path = os.path.join(SCRIPTS, 'narc_0205.bin')
    text = dpscript.disasm(open(path, 'rb').read())
    if 'Unk0221' in text:
        print('narc_0205: Move Tutor already patched')
        return
    assert '; script 52' in text and '; script 53' not in text
    text = text.replace('.endscripts', '.script L_GUNK_TUTOR ; 53\n.endscripts')
    text = text.rstrip() + '\n' + TUTOR
    data = dpscript.asm(text)
    check = dpscript.disasm(data)
    assert '; script 53' in check and 'Unk0221' in check, 'reassembled scripts lost the Move Tutor'
    open(path, 'wb').write(data)
    print('narc_0205: Move Tutor patched (%d bytes)' % len(data))


def map_events_files():
    """events bank of each Pokemon Center 1F, from arm9/src/map_header.c (rows are in map id order)"""
    import re
    maps = {}
    for line in open(os.path.join(ROOT, 'include', 'constants', 'maps.h')):
        m = re.match(r'#define (MAP_\w+)\s+(\d+)', line)
        if m:
            maps[m.group(1)] = int(m.group(2))
    rows = [l for l in open(os.path.join(ROOT, 'arm9', 'src', 'map_header.c')) if l.strip().startswith('{ NARC_area_data')]
    out = {}
    for name in POKECENTER_1F_MAPS:
        row = rows[maps[name]]
        assert name.replace('_1F', '') in row, (name, row[-60:])
        out[name] = int(re.search(r'zone_event_release_narc_(\d+)_bin', row).group(1))
    return out


def patch_tutor_events():
    import struct
    for name, n in map_events_files().items():
        path = os.path.join(ROOT, 'files', 'fielddata', 'eventdata', 'zone_event_release', 'narc_%04d.bin' % n)
        d = bytearray(open(path, 'rb').read())
        o = 0
        nbg = struct.unpack_from('<I', d, o)[0]
        o += 4 + nbg * 0x14
        nobj_at = o
        nobj = struct.unpack_from('<I', d, o)[0]
        o += 4
        objs = [struct.unpack_from('<14Hi', d, o + 0x20 * i) for i in range(nobj)]
        if any(obj[5] == SCRIPT_MOVE_TUTOR for obj in objs):
            print('%s: Move Tutor already there' % name)
            continue
        x, z = TUTOR_POSITION.get(name, (TUTOR_X, TUTOR_Z))
        assert not any(obj[12] == x and obj[13] == z for obj in objs), (name, 'tile taken')
        local_id = max([obj[0] for obj in objs] + [-1]) + 1
        tutor = struct.pack('<14Hi', local_id, SPRITE_GENTLEMAN, MOVEMENT_STILL, 0, 0, SCRIPT_MOVE_TUTOR, FACING_EAST,
                            0, 0, 0, 0, 0, x, z, 0)
        end_of_objs = o + 0x20 * nobj
        d[end_of_objs:end_of_objs] = tutor
        struct.pack_into('<I', d, nobj_at, nobj + 1)
        open(path, 'wb').write(d)
        print('%s (events %d): Move Tutor added as object %d' % (name, n, local_id))




ITEM_DEXNAV = 465

TUTORIAL_BALLS = """    SetVar 0x8004, 4
    SetVar 0x8005, 5
    RunScriptWait 0x7FC
"""

TUTORIAL_BALLS_AND_DEXNAV = TUTORIAL_BALLS + """    Unk002C 14
    SetVar 0x8004, 465
    SetVar 0x8005, 1
    RunScriptWait 0x7FC
    UnlockPoketchApp 25
    Unk002C 15
"""

# earlier builds gave the DexNav without its Pokétch app (app 25)
GIFT_WITHOUT_APP = """    SetVar 0x8005, 1
    RunScriptWait 0x7FC
    Unk002C 0xF
"""
GIFT_WITH_APP = """    SetVar 0x8005, 1
    RunScriptWait 0x7FC
    UnlockPoketchApp 25
    Unk002C 0xF
"""


def patch_route202():
    path = os.path.join(SCRIPTS, 'narc_0393.bin')
    text = dpscript.disasm(open(path, 'rb').read())
    if 'UnlockPoketchApp 0x19' in text or 'UnlockPoketchApp 25' in text:
        print('narc_0393: already patched')
        return
    if 'Unk002C 0xF' in text:
        assert text.count(GIFT_WITHOUT_APP) == 1, 'unexpected DexNav gift script'
        open(path, 'wb').write(dpscript.asm(text.replace(GIFT_WITHOUT_APP, GIFT_WITH_APP)))
        print('narc_0393: added the DexNav Pokétch app')
        return
    assert text.count(TUTORIAL_BALLS) == 1, 'unexpected catching tutorial script'
    text = text.replace(TUTORIAL_BALLS, TUTORIAL_BALLS_AND_DEXNAV)
    data = dpscript.asm(text)
    check = dpscript.disasm(data)
    assert check.count('RunScriptWait 0x7FC') == 2 and 'UnlockPoketchApp' in check and ('Unk002C 0xF' in check or 'Unk002C 15' in check),         'reassembled script lost the gift'
    open(path, 'wb').write(data)
    print('narc_0393: patched (%d bytes)' % len(data))


# ---- Honey trees say how long until Pokemon gather (opcode 725, message 147) ----
HONEY_NOT_READY_OLD = """    Unk002C 0x30
    WaitButton
    CloseMessageBox
    ReleaseAllEvents
    End
"""

HONEY_NOT_READY_NEW = """    Gunk_HoneyTreeHours 0x800C
    CompareVarToValue 0x800C, 0
    GoToIf 1, L_GUNK_HONEY_PLAIN
    Unk002C 147
    WaitButton
    CloseMessageBox
    ReleaseAllEvents
    End

L_GUNK_HONEY_PLAIN:
    Unk002C 0x30
    WaitButton
    CloseMessageBox
    ReleaseAllEvents
    End
"""


HONEY_HAS_HONEY_NEW = """    Gunk_HoneyTreeHours 0x800C
    CompareVarToValue 0x800C, 0
    GoToIf 1, L_GUNK_HONEY_PLAIN2
    Unk002C 147
    WaitButton
    GoTo %s

L_GUNK_HONEY_PLAIN2:
    Unk002C 0x31
    GoTo %s
"""


def patch_honey():
    path = os.path.join(SCRIPTS, 'narc_0205.bin')
    text = dpscript.disasm(open(path, 'rb').read())
    if 'Gunk_HoneyTreeHours' in text:
        print('narc_0205: honey tree already patched')
        return
    assert text.count(HONEY_NOT_READY_OLD) == 1, 'unexpected honey tree script'
    text = text.replace(HONEY_NOT_READY_OLD, HONEY_NOT_READY_NEW)
    # with Honey in the Bag: say the hours, then offer to slather more as before
    m = re.findall(r'    Unk002C 0x31\n    GoTo (L_[0-9A-F]+)\n', text)
    assert len(m) == 1, 'unexpected honey tree script (has Honey)'
    text = text.replace('    Unk002C 0x31\n    GoTo %s\n' % m[0], HONEY_HAS_HONEY_NEW % (m[0], m[0]))
    data = dpscript.asm(text)
    assert 'Gunk_HoneyTreeHours' in dpscript.disasm(data), 'reassembled scripts lost the honey tree change'
    open(path, 'wb').write(data)
    print('narc_0205: honey tree patched (%d bytes)' % len(data))


PARTS = {'common': lambda: patch_common(), 'tutor': lambda: (patch_tutor_script(), patch_tutor_events()),
         'route202': lambda: patch_route202(), 'honey': lambda: patch_honey()}

if __name__ == '__main__':
    for part in sys.argv[1:] or list(PARTS):
        PARTS[part]()
