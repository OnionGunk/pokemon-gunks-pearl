"""Find (and with --apply, fix) raw code/data addresses inside .byte data in the decomp's asm.

pret's disassembly left some tables as raw bytes. A pointer stored that way keeps its vanilla value when the code
it points at moves (arm9 grows, an overlay loads elsewhere), so the game jumps into the wrong code.

Labels in the asm carry their vanilla address in a comment ("name: ; 0x0223AE90"); C symbols come from a vanilla
xMAP. A 4-byte-aligned word inside a .byte run is a candidate when it equals a label address (data or ARM), or a
thumb function's address + 1. With --apply each candidate's 4 bytes become ".word <label>" (the assembler adds the
thumb bit for thumb functions, as pret's own .word tables do). Review the printed list first: a value can match by
chance (pairs of u16 values often look like 0x021E0001).
Found and fixed this way: scrcmd_9.s's TV-program table and overlay_83.s's ov83_0223AEB0 (the map-arrival freeze).

    py rawptr_fix.py <vanilla.xMAP> <repo> [--apply] [--only file1,file2]
"""
import os
import re
import sys

xmap, repo = sys.argv[1], sys.argv[2]
apply = '--apply' in sys.argv
only = None
if '--only' in sys.argv:
    only = set(sys.argv[sys.argv.index('--only') + 1].split(','))

labels = {}   # vanilla addr -> [names] (overlays share address ranges, so one address can have several)
thumb = set()
for root, _, files in os.walk(os.path.join(repo, 'arm9')):
    for f in files:
        if not f.endswith('.s'):
            continue
        prev_thumb = None
        for line in open(os.path.join(root, f), errors='replace'):
            m = re.match(r'\s*thumb_func_start\s+(\w+)', line)
            if m:
                thumb.add(m.group(1))
            m = re.match(r'^(\w+):\s*;\s*0x([0-9A-Fa-f]{8})', line)
            if m:
                labels.setdefault(int(m.group(2), 16), []).append(m.group(1))
for line in open(xmap, errors='replace'):
    m = re.match(r'\s+([0-9A-F]{8}) [0-9A-F]{8} \.(\w+)\s+(\S+)\t', line)
    if m and not m.group(3).startswith(('$', '.')):
        a = int(m.group(1), 16)
        if 0x02000800 <= a < 0x02400000:
            labels.setdefault(a, []).append(m.group(3))
            if m.group(2) == 'text' and a & 1:
                pass

# thumb C functions: the xMAP lists them at even addresses; treat names from C (no asm label) as thumb when the
# C object is thumb (all of pret's C is thumb)
c_names = set()
for line in open(xmap, errors='replace'):
    m = re.match(r'\s+([0-9A-F]{8}) [0-9A-F]{8} \.text\s+(\S+)\t(\S+)\.o', line)
    if m and not m.group(2).startswith(('$', '.')):
        c_names.add(m.group(2))

def pick(names, overlay):
    # prefer a label of the file's own overlay, then a main-binary label; other overlays aren't loaded with it
    own = [n for n in names if overlay is not None and n.startswith(f'ov{overlay:02d}_')]
    main = [n for n in names if not re.match(r'ov\d\d_', n)]
    return (own or main or [None])[0]

def match(v, overlay):
    if v & 1:
        name = pick(labels.get(v - 1, []), overlay)
        if name and (name in thumb or name in c_names):
            return name
        return None
    name = pick(labels.get(v, []), overlay)
    if name and name not in thumb and v & 3 == 0:
        return name
    return None

num = re.compile(r'^(\s*)\.byte\s+(.*?)\s*$')
total = 0
for root, _, files in os.walk(os.path.join(repo, 'arm9')):
    for f in files:
        if not f.endswith('.s') or (only and f not in only):
            continue
        path = os.path.join(root, f)
        om = re.search(r'overlays[\\/](\d+)', path)
        overlay = int(om.group(1)) if om else None
        lines = open(path, errors='replace', newline='').read().split('\n')
        # collect contiguous .byte runs; offset counted from the last label (tables start aligned)
        out = []
        changed = False
        run = []  # (line_index, [bytes])
        def process():
            global total, changed
            if not run:
                return
            flat = []  # (byte, run_idx, pos_in_line)
            for ri, (li, bs) in enumerate(run):
                for pi, b in enumerate(bs):
                    flat.append((b, ri, pi))
            hits = {}
            for i in range(0, len(flat) - 3, 4):
                v = flat[i][0] | flat[i + 1][0] << 8 | flat[i + 2][0] << 16 | flat[i + 3][0] << 24
                if 0x02000800 <= v < 0x02400000:
                    name = match(v, overlay)
                    if name:
                        hits[i] = name
                        total += 1
                        print(f'{os.path.relpath(path, repo)}:{run[flat[i][1]][0] + 1}: {v:#010x} -> {name}')
            if hits and apply:
                # rewrite the run: emit bytes, replacing hit words by .word
                new = []
                i = 0
                indent = '\t'
                pending = []
                def flush_bytes():
                    if pending:
                        new.append(indent + '.byte ' + ', '.join(f'0x{b:02X}' for b in pending))
                        pending.clear()
                while i < len(flat):
                    if i in hits:
                        flush_bytes()
                        new.append(f'{indent}.word {hits[i]}')
                        i += 4
                        continue
                    pending.append(flat[i][0])
                    if len(pending) == 16:
                        flush_bytes()
                    i += 1
                flush_bytes()
                first = run[0][0]
                for li, _ in run:
                    lines[li] = None
                lines[first] = '\n'.join(new)
                changed = True
            run.clear()
        for li, line in enumerate(lines):
            m = num.match(line.split(';')[0])
            if m:
                try:
                    bs = [int(x, 0) for x in m.group(2).split(',') if x.strip()]
                except ValueError:
                    process()
                    continue
                run.append((li, bs))
            else:
                process()
        process()
        if changed:
            open(path, 'w', newline='').write('\n'.join(l for l in lines if l is not None))
print(f'{total} candidates', file=sys.stderr)
