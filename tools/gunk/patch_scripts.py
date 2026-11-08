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
"""
import os
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
    .hword {CMD_CHECK_REPEL_AGAIN}
    .hword 0x800C
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
    .hword {CMD_USE_REPEL_AGAIN}
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
    .hword {CMD_TOGGLE_ENDLESS_REPEL}
    .hword 0x800C
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
    .hword {CMD_TOGGLE_EXP_ALL}
    .hword 0x800C
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
    if 'L_GUNK' in text or '.hword 721' in text:
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


if __name__ == '__main__':
    patch_common()
