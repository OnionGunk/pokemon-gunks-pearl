#!/usr/bin/env python3
"""Pokemon Diamond/Pearl field-script (scr_seq) disassembler / assembler / patcher.

CLI:
    python dpscript.py disasm narc_0001.bin [-o out.s]
    python dpscript.py asm in.s -o narc_0001.bin

Library:
    from dpscript import disasm, asm, Script
    text = disasm("narc_0001.bin")          # path or bytes
    data = asm(text)                         # -> bytes
    s = Script.load("narc_0001.bin")         # structured patch API (see class Script)

Text format (one statement per line, ';' starts a comment):
    .script <label>            header entry (script id = position, 1-based)
    .endscripts                header terminator (u16 0xFD13)
    <label>:                   label definition
    <CmdName> a, b, ...        a command (ScrCmd_ prefix optional; Cmd_XXXX = raw opcode)
    .mov <cmd>, <count>        one movement step (u16 cmd, u16 count); cmd 0xFE ends a block
    .byte / .hword / .word     raw data
Numbers may be decimal, 0x hex or negative; r/m arguments take a label (or a number = raw relative offset).
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdsizes import CMDS, TERMINATORS  # noqa: E402

HEADER_END = 0xFD13
MOVE_END = 0xFE
ARG_SIZE = {"b": 1, "h": 2, "v": 2, "p": 2, "w": 4, "r": 4, "m": 4}

NAME_TO_OP = {}
for _op, _c in CMDS.items():
    NAME_TO_OP[_c["name"]] = _op
    NAME_TO_OP[_c["name"][len("ScrCmd_"):]] = _op
TERMINATOR_OPS = {NAME_TO_OP[n] for n in TERMINATORS}


def short_name(op):
    return CMDS[op]["name"][len("ScrCmd_"):]


class DisasmError(Exception):
    pass


# ============================================================================ decoding
def _read(data, pos, t):
    size = ARG_SIZE[t]
    if pos + size > len(data):
        raise DisasmError("argument runs past end of file at 0x%X" % pos)
    if size == 1:
        return data[pos]
    if size == 2:
        return struct.unpack_from("<H", data, pos)[0]
    if t in "rm":
        return struct.unpack_from("<i", data, pos)[0]
    return struct.unpack_from("<I", data, pos)[0]


def layout_for(op, selector=None):
    """Return the arg list for an opcode. For switch layouts, `selector` must be given."""
    a = CMDS[op]["args"]
    if isinstance(a, list):
        return a
    sel = a["switch"]
    if selector is None:
        return [sel]
    return [sel] + list(a["cases"].get(selector, a["default"]))


def decode_cmd(data, pos):
    """Decode one command at pos -> (op, [(type, value)], size). Raises DisasmError."""
    if pos + 2 > len(data):
        raise DisasmError("opcode runs past end of file at 0x%X" % pos)
    op = struct.unpack_from("<H", data, pos)[0]
    if op not in CMDS or CMDS[op]["args"] is None:
        raise DisasmError("unknown opcode 0x%04X at 0x%X" % (op, pos))
    p = pos + 2
    args = []
    a = CMDS[op]["args"]
    types = a if isinstance(a, list) else [a["switch"]]
    for t in types:
        v = _read(data, p, t)
        p += ARG_SIZE[t]
        if t in "rm":
            v = p + v  # absolute target
        args.append((t, v))
    if not isinstance(a, list):
        for t in a["cases"].get(args[0][1], a["default"]):
            v = _read(data, p, t)
            p += ARG_SIZE[t]
            if t in "rm":
                v = p + v
            args.append((t, v))
    return op, args, p - pos


def parse_header(data):
    """-> (entry addresses, header size, has_terminator).
    The header is a list of s32 offsets (relative to the end of each) terminated by u16 0xFD13.
    A few files omit the terminator: the table then ends where the first script begins."""
    entries, pos = [], 0
    first = len(data)
    while True:
        if pos >= first:
            if not entries:
                raise DisasmError("empty header")
            return entries, pos, False
        if pos + 2 > len(data):
            raise DisasmError("no 0xFD13 header terminator")
        if struct.unpack_from("<H", data, pos)[0] == HEADER_END:
            return entries, pos + 2, True
        if pos + 4 > len(data):
            raise DisasmError("header runs past end")
        off = struct.unpack_from("<i", data, pos)[0]
        t = pos + 4 + off
        if not 0 <= t < len(data) or t < pos + 4:
            raise DisasmError("header entry %d points outside the file (0x%X)" % (len(entries), t))
        entries.append(t)
        first = min(first, t)
        pos += 4


class Analysis:
    """Result of walking a script file: what every byte is."""

    def __init__(self, data, speculate=True):
        self.data = data
        self.spec = set()       # start addresses of speculatively decoded (unreferenced) blocks
        self.spec_items = set()
        self.entries, self.header_size, self.header_term = parse_header(data)
        self.owner = [None] * len(data)   # per byte: start address of the item covering it
        self.items = {}                   # start -> (kind, payload, size); kind in cmd/mov
        self.code_labels, self.mov_labels = set(), set()
        self.errors = []
        self._walk(speculate)

    def _claim(self, start, size):
        for i in range(start, start + size):
            if self.owner[i] is not None:
                return False
        for i in range(start, start + size):
            self.owner[i] = start
        return True

    def _walk(self, speculate):
        data = self.data
        for i in range(self.header_size):
            self.owner[i] = -1
        todo = []
        for e in self.entries:
            if 0 <= e < len(data):
                self.code_labels.add(e)
                todo.append(e)
            else:
                self.errors.append("entry points outside file: 0x%X" % e)
        movs = []
        self.error_ctx = []
        while todo:
            pos = todo.pop()
            run = []
            while True:
                if pos in self.items:
                    if self.items[pos][0] != "cmd":
                        self.errors.append("code flows into movement data at 0x%X" % pos)
                    break
                if pos >= len(data):
                    self.errors.append("code runs off end of file at 0x%X" % pos)
                    break
                if self.owner[pos] is not None:
                    self.errors.append("code at 0x%X overlaps item at 0x%X" % (pos, self.owner[pos]))
                    break
                try:
                    op, args, size = decode_cmd(data, pos)
                except DisasmError as ex:
                    self.errors.append(str(ex))
                    self.error_ctx.append((str(ex), run[-6:]))
                    break
                if not self._claim(pos, size):
                    self.errors.append("command at 0x%X overlaps another item" % pos)
                    break
                self.items[pos] = ("cmd", (op, args), size)
                run.append(pos)
                for t, v in args:
                    if t == "r":
                        if 0 <= v < len(data):
                            self.code_labels.add(v)
                            todo.append(v)
                        else:
                            self.errors.append("jump target 0x%X out of range (cmd at 0x%X)" % (v, pos))
                    elif t == "m":
                        if 0 <= v < len(data):
                            self.mov_labels.add(v)
                            movs.append(v)
                        else:
                            self.errors.append("movement target 0x%X out of range (cmd at 0x%X)" % (v, pos))
                pos += size
                if op in TERMINATOR_OPS:
                    break
        for start in sorted(set(movs)):
            pos = start
            while True:
                if pos in self.items:
                    if self.items[pos][0] != "mov":
                        self.errors.append("movement at 0x%X runs into code" % start)
                    break  # shared tail
                if pos + 4 > len(data):
                    self.errors.append("movement at 0x%X runs off end of file" % start)
                    break
                if not self._claim(pos, 4):
                    self.errors.append("movement at 0x%X overlaps item at 0x%X" % (pos, self.owner[pos]))
                    break
                c, n = struct.unpack_from("<HH", data, pos)
                self.items[pos] = ("mov", (c, n), 4)
                pos += 4
                if c == MOVE_END:
                    break
        if speculate:
            self._speculate()

    # ------------------------------------------------------------------ unreferenced code
    def gaps(self):
        res, i, n = [], 0, len(self.data)
        while i < n:
            if self.owner[i] is None:
                j = i
                while j < n and self.owner[j] is None:
                    j += 1
                res.append((i, j))
                i = j
            else:
                i += 1
        return res

    def _trial(self, start):
        """Decode unreachable bytes at `start` as code (following its own jumps/movement into other
        unclaimed bytes). Returns {addr: item} if everything decodes cleanly, else None."""
        data, new, owner = self.data, {}, {}
        todo, movs = [start], []

        def free(a, size):
            return all(0 <= k < len(data) and self.owner[k] is None and k not in owner for k in range(a, a + size))

        while todo:
            pos = todo.pop()
            while True:
                if pos in new or (pos in self.items and self.items[pos][0] == "cmd"):
                    break  # joins known code
                if not free(pos, 2):
                    return None
                try:
                    op, args, size = decode_cmd(data, pos)
                except DisasmError:
                    return None
                if not free(pos, size):
                    return None
                new[pos] = ("cmd", (op, args), size)
                for k in range(pos, pos + size):
                    owner[k] = pos
                for t, v in args:
                    if not 0 <= v < len(data):
                        if t in "rm":
                            return None
                    if t == "r":
                        if v in self.items and self.items[v][0] != "cmd":
                            return None
                        if v not in self.items and self.owner[v] is not None:
                            return None
                        todo.append(v)
                    elif t == "m":
                        movs.append(v)
                pos += size
                if op in TERMINATOR_OPS:
                    break
        for pos in movs:
            while True:
                if pos in new and new[pos][0] == "mov" or pos in self.items and self.items[pos][0] == "mov":
                    break
                if not free(pos, 4):
                    return None
                c, n = struct.unpack_from("<HH", data, pos)
                new[pos] = ("mov", (c, n), 4)
                for k in range(pos, pos + 4):
                    owner[k] = pos
                pos += 4
                if c == MOVE_END:
                    break
        return new

    def _speculate(self):
        """Classify unreferenced bytes (dead code after a GoTo, unused scripts) as code where they decode
        cleanly. All-zero runs are left as data (alignment padding)."""
        progress = True
        while progress:
            progress = False
            for a, b in self.gaps():
                if not any(self.data[a:b]):
                    continue
                f = next(i for i in range(a, b) if self.data[i])   # skip leading zero padding
                new, is_mov = None, False
                for start in sorted({a, f, f & ~3, (a + 3) & ~3}):
                    if start < a:
                        continue
                    if start % 4 == 0:
                        new = self._trial_mov(start, b)
                        if new:
                            is_mov = True
                            break
                    new = self._trial(start)
                    if new:
                        break
                if not new:
                    continue
                a = start
                self.spec.add(a)
                for pos, it in new.items():
                    self.items[pos] = it
                    self.spec_items.add(pos)
                    for k in range(pos, pos + it[2]):
                        self.owner[k] = pos
                    if it[0] == "cmd":
                        for t, v in it[1][1]:
                            if t == "r":
                                self.code_labels.add(v)
                            elif t == "m":
                                self.mov_labels.add(v)
                (self.mov_labels if is_mov else self.code_labels).add(a)
                progress = True
                break

    def _trial_mov(self, a, b):
        """Unreferenced movement block: 4-aligned (u16 cmd <= 0xFE, u16 count < 0x100) pairs ending in 0xFE,
        fully inside the gap [a, b)."""
        new, pos = {}, a
        while pos + 4 <= b:
            c, n = struct.unpack_from("<HH", self.data, pos)
            if c > MOVE_END or n >= 0x100:
                return None
            new[pos] = ("mov", (c, n), 4)
            pos += 4
            if c == MOVE_END:
                return new
        return None

    def coverage(self):
        cmd = sum(s for k, _, s in self.items.values() if k == "cmd")
        mov = sum(s for k, _, s in self.items.values() if k == "mov")
        hdr = self.header_size
        zero = sum(1 for i, o in enumerate(self.owner) if o is None and self.data[i] == 0)
        spec = sum(self.items[p][2] for p in self.spec_items)
        return dict(total=len(self.data), header=hdr, cmd=cmd, mov=mov, data=len(self.data) - hdr - cmd - mov,
                    zero=zero, spec=spec)


# ============================================================================ level scripts
# Record types seen in the 512 level-script files (meanings of 2..4 are not confirmed from the code).
LEVEL_TYPES = {1: "type 1: var/value -> script table (checked by the field loop)", 2: "type 2: script id",
               3: "type 3: script id", 4: "type 4: script id"}


def parse_level(data):
    """Level-script (map init) file: records [u8 type, u32 value] until u8 0.
    type 1: value = s32 offset (relative to end of the u32) to a table of (u16 var, u16 value, u16 script)
    rows ended by u16 0; other types: value = script id. -> (records, tables{addr: rows}, end)"""
    pos, recs, tables = 0, [], {}
    while True:
        if pos >= len(data):
            raise DisasmError("level script: no terminator")
        t = data[pos]
        if t == 0:
            pos += 1
            break
        if t not in LEVEL_TYPES or pos + 5 > len(data):
            raise DisasmError("level script: bad record type %d at 0x%X" % (t, pos))
        v = struct.unpack_from("<i" if t == 1 else "<I", data, pos + 1)[0]
        if t == 1:
            v = pos + 5 + v
            if not 0 <= v < len(data):
                raise DisasmError("level script: table offset out of range")
            tables[v] = None
        recs.append((t, v))
        pos += 5
    end = pos
    for tb in tables:
        p, rows = tb, []
        while True:
            if p + 2 > len(data):
                raise DisasmError("level script: table at 0x%X runs off end" % tb)
            a = struct.unpack_from("<H", data, p)[0]
            if a == 0:
                p += 2
                break
            if p + 6 > len(data):
                raise DisasmError("level script: table at 0x%X runs off end" % tb)
            rows.append(struct.unpack_from("<HHH", data, p))
            p += 6
        tables[tb] = (rows, p)
        end = max(end, p)
    return recs, tables, end


def disasm_level(data):
    recs, tables, end = parse_level(data)
    out = ["; dpscript level-script (map init) file, %d bytes" % len(data), ".levelscripts"]
    for t, v in recs:
        if t == 1:
            out.append("    .ls_table LT_%04X ; %s" % (v, LEVEL_TYPES[t]))
        else:
            out.append("    .ls %d, 0x%X ; %s" % (t, v, LEVEL_TYPES[t]))
    out.append("    .ls_end")
    pos = len(recs) * 5 + 1
    for tb in sorted(tables):
        rows, tend = tables[tb]
        if tb != pos:
            if tb < pos:
                raise DisasmError("level script: overlapping tables")
            out.append("    .byte " + ", ".join("0x%02X" % b for b in data[pos:tb]))
        out.append("LT_%04X:" % tb)
        for a, b, c in rows:
            out.append("    .lst 0x%X, %d, 0x%X ; if var 0x%X == %d run script %d" % (a, b, c, a, b, c))
        out.append("    .lst_end")
        pos = tend
    if pos < len(data):
        out.append("    .byte " + ", ".join("0x%02X" % b for b in data[pos:]) + " ; padding")
    return "\n".join(out) + "\n"


def _raw_text(data, head):
    out = [head.rstrip("\n")]
    for k in range(0, len(data), 16):
        out.append("    .byte " + ", ".join("0x%02X" % b for b in data[k:k + 16]))
    return "\n".join(out) + "\n"


# ============================================================================ text output
def _fmt_num(v):
    return "0x%X" % v if v > 9 else str(v)


def kind_for_path(path):
    """'level' for map level-script files (scr_seq narc members 465..976, per map_header.c), else 'script'."""
    m = re.search(r"narc_(\d+)\.bin$", str(path))
    if m and 465 <= int(m.group(1)) <= 976:
        return "level"
    return "script"


def guess_kind(data):
    try:
        an = Analysis(data)
        if not an.errors:
            return "script"
    except DisasmError:
        pass
    try:
        parse_level(data)
        return "level"
    except DisasmError:
        return "raw"


def disasm(src, kind=None, speculate=True):
    """Disassemble a file (path or bytes) to text. kind: 'script' | 'level' | 'raw' | None (auto).
    speculate: also decode unreferenced bytes that look like code/movement (dead code, unused scripts)."""
    if isinstance(src, (str, os.PathLike)):
        data = open(src, "rb").read()
        kind = kind or kind_for_path(src)
    else:
        data = bytes(src)
    if not data:
        return "; dpscript: empty file\n"
    kind = kind or guess_kind(data)
    if kind == "level":
        return disasm_level(data)
    if kind == "raw":
        return _raw_text(data, "; dpscript: unrecognised file, raw bytes\n")
    an = Analysis(data, speculate)

    def label(addr):
        if addr in an.code_labels:
            return "L_%04X" % addr
        if addr in an.mov_labels:
            return "M_%04X" % addr
        return "D_%04X" % addr

    out = ["; dpscript disassembly (%d bytes, %d scripts)" % (len(data), len(an.entries))]
    for e in an.errors:
        out.append("; WARNING: " + e)
    for i, e in enumerate(an.entries):
        if 0 <= e < len(data):
            out.append(".script %s ; %d" % (label(e), i + 1))
        else:
            out.append(".script %d ; %d (raw offset, outside file)" % (e - (i * 4 + 4), i + 1))
    out.append(".endscripts" if an.header_term else ".endscripts noterm ; header has no 0xFD13 terminator")
    entry_ids = {}
    for i, e in enumerate(an.entries):
        entry_ids.setdefault(e, []).append(i + 1)

    pos = an.header_size
    n = len(data)
    while pos < n:
        it = an.items.get(pos)
        if pos in an.code_labels or pos in an.mov_labels:
            out.append("")
            if pos in an.mov_labels and it is not None and pos % 4 == 0 and out[-2] != "    .align 4":
                out.append("    .align 4")  # no-op here; keeps the block 4-aligned if code before it moves
            ids = entry_ids.get(pos)
            note = " ; script %s" % ", ".join(map(str, ids)) if ids else ""
            if pos in an.spec:
                note += " ; unreferenced (decoded speculatively)"
            out.append(label(pos) + ":" + note)
        if it is None:
            # raw data run until next item/label
            end = pos + 1
            while end < n and an.owner[end] is None and end not in an.code_labels and end not in an.mov_labels:
                end += 1
            chunk = data[pos:end]
            nxt_mov = end == n or (end in an.items and an.items[end][0] == "mov")
            if not any(chunk) and end % 4 == 0 and len(chunk) < 4 and nxt_mov:
                out.append("    .align 4")  # zero padding that 4-aligns a movement block / end of file
                pos = end
                continue
            if pos not in an.code_labels and pos not in an.mov_labels:
                out.append("")
                out.append("D_%04X:" % pos)
            for k in range(0, len(chunk), 16):
                out.append("    .byte " + ", ".join("0x%02X" % b for b in chunk[k:k + 16]))
            pos = end
            continue
        kind, payload, size = it
        if kind == "cmd":
            op, args = payload
            parts = []
            for t, v in args:
                parts.append(label(v) if t in "rm" else _fmt_num(v))
            out.append("    %s %s" % (short_name(op), ", ".join(parts)) if parts else "    " + short_name(op))
            if op in TERMINATOR_OPS:
                out.append("")
        else:
            c, cnt = payload
            out.append("    .mov 0x%02X, %d" % (c, cnt))
        pos += size
    return "\n".join(out) + "\n"


# ============================================================================ assembler
_LABEL_RE = re.compile(r"^([A-Za-z_.$][\w.$]*):\s*(.*)$")


def _num(s):
    s = s.strip()
    return int(s, 0)


def _split_args(s):
    s = s.strip()
    return [a.strip() for a in s.split(",")] if s else []


def parse_text(text):
    """Text -> list of statements: ('label', name) | ('script', ref) | ('endscripts',) |
    ('cmd', op, [argstr]) | ('dir', name, [argstr])"""
    stmts = []
    for lineno, raw in enumerate(text.split("\n"), 1):
        line = raw.split(";", 1)[0].strip()
        while line:
            m = _LABEL_RE.match(line)
            if m and not line.startswith("."):
                stmts.append(("label", m.group(1)))
                line = m.group(2).strip()
                continue
            break
        if not line:
            continue
        parts = line.split(None, 1)
        word, rest = parts[0], parts[1] if len(parts) > 1 else ""
        if word.startswith("."):
            d = word.lower()
            if d == ".script":
                stmts.append(("script", rest.strip()))
            elif d == ".endscripts":
                stmts.append(("endscripts", rest.strip().lower() != "noterm"))
            elif d == ".levelscripts":
                continue
            elif d in (".byte", ".hword", ".short", ".word", ".mov", ".ls", ".ls_table", ".ls_end",
                       ".lst", ".lst_end", ".rel", ".align"):
                stmts.append(("dir", d, _split_args(rest)))
            else:
                raise SyntaxError("line %d: unknown directive %s" % (lineno, word))
            continue
        if word in NAME_TO_OP:
            op = NAME_TO_OP[word]
        elif re.match(r"^Cmd_[0-9A-Fa-f]+$", word):
            op = int(word[4:], 16)
            if op not in CMDS:
                raise SyntaxError("line %d: opcode out of range %s" % (lineno, word))
        else:
            raise SyntaxError("line %d: unknown command %s" % (lineno, word))
        stmts.append(("cmd", op, _split_args(rest)))
    return stmts


def _cmd_types(op, args):
    a = CMDS[op]["args"]
    if isinstance(a, list):
        types = list(a)
    else:
        sel = _num(args[0]) if args else None
        types = layout_for(op, sel)
    if len(types) != len(args):
        raise SyntaxError("%s expects %d args (%s), got %d: %r" % (short_name(op), len(types), "".join(types),
                                                                     len(args), args))
    return types


def _stmt_size(st):
    k = st[0]
    if k == "label":
        return 0
    if k == "script":
        return 4
    if k == "endscripts":
        return 2 if st[1] else 0
    if k == "cmd":
        return 2 + sum(ARG_SIZE[t] for t in _cmd_types(st[1], st[2]))
    d, args = st[1], st[2]
    fixed = {".align": 0, ".ls": 5, ".ls_table": 5, ".ls_end": 1, ".lst": 6, ".lst_end": 2}
    if d in fixed:
        return fixed[d]
    return {".byte": 1, ".hword": 2, ".short": 2, ".word": 4, ".mov": 2, ".rel": 4}[d] * len(args)


def _align_pad(st, pos):
    return (-pos) % _num(st[2][0]) if st[0] == "dir" and st[1] == ".align" else 0


def assemble_stmts(stmts):
    labels, pos = {}, 0
    for st in stmts:
        if st[0] == "label":
            if st[1] in labels:
                raise SyntaxError("duplicate label " + st[1])
            labels[st[1]] = pos
        pos += _align_pad(st, pos) or _stmt_size(st)
    out = bytearray()

    def val(s):
        return labels[s] if s in labels else _num(s)

    for st in stmts:
        k = st[0]
        if k == "label":
            continue
        if k == "script":
            ref = st[1]
            here = len(out) + 4
            off = labels[ref] - here if ref in labels else _num(ref)
            out += struct.pack("<i", off)
        elif k == "endscripts":
            if st[1]:
                out += struct.pack("<H", HEADER_END)
        elif k == "cmd":
            op, args = st[1], st[2]
            types = _cmd_types(op, args)
            out += struct.pack("<H", op)
            for t, a in zip(types, args):
                if t == "b":
                    out += struct.pack("<B", val(a) & 0xFF)
                elif t in "hvp":
                    out += struct.pack("<H", val(a) & 0xFFFF)
                elif t == "w":
                    out += struct.pack("<I", val(a) & 0xFFFFFFFF)
                else:  # r / m: relative to end of the field
                    end = len(out) + 4
                    rel = labels[a] - end if a in labels else _num(a)
                    out += struct.pack("<i", rel)
        else:
            d, args = st[1], st[2]
            if d == ".align":
                out += bytes(_align_pad(st, len(out)))
                continue
            if d == ".ls":
                out += struct.pack("<BI", val(args[0]) & 0xFF, val(args[1]) & 0xFFFFFFFF)
                continue
            if d == ".ls_table":
                end = len(out) + 5
                out += struct.pack("<Bi", 1, labels[args[0]] - end if args[0] in labels else _num(args[0]))
                continue
            if d == ".ls_end":
                out += bytes(1)
                continue
            if d == ".lst":
                out += struct.pack("<HHH", *(val(a) & 0xFFFF for a in args))
                continue
            if d == ".lst_end":
                out += bytes(2)
                continue
            for a in args:
                if d == ".rel":
                    end = len(out) + 4
                    out += struct.pack("<i", labels[a] - end if a in labels else _num(a))
                    continue
                v = val(a)
                if d == ".byte":
                    out += struct.pack("<B", v & 0xFF)
                elif d in (".hword", ".short", ".mov"):
                    out += struct.pack("<H", v & 0xFFFF)
                else:
                    out += struct.pack("<I", v & 0xFFFFFFFF)
    return bytes(out)


def asm(text):
    """Assemble dpscript text to bytes."""
    return assemble_stmts(parse_text(text))


# ============================================================================ patch API
class Script:
    """Structured, label-based view of a script file for patching.

    All jumps/calls/movement pointers are symbolic, so inserting or appending code and
    reassembling recomputes every relative offset.  Bytes the walker could not classify are
    kept as raw .byte data (see Analysis.coverage()); never insert code *inside* such a run.

        s = Script.load(path)
        sid = s.add_script("PlayFanfare 0x5DC\\nEnd")          # new entry -> returns script id
        s.replace_command("L_000E", 2, "SetFlag 0x123")         # 3rd command after label
        s.insert_call("L_000E", 0, "SetVar 0x4000, 1\\n")        # Call new block (auto Return)
        s.insert_jump("L_000E", 4, "...code...\\nEnd")           # replace cmd #4 with GoTo new block
        s.insert_before("L_000E", 1, "Nop")                      # raw insertion
        open(path, "wb").write(s.to_bytes())
    """

    def __init__(self, text):
        self.stmts = parse_text(text)
        self._n = 0

    @classmethod
    def load(cls, src, speculate=True):
        return cls(disasm(src, speculate=speculate))

    # -- helpers
    def _new_label(self, prefix="P"):
        names = {st[1] for st in self.stmts if st[0] == "label"}
        while True:
            self._n += 1
            name = "%s_%03d" % (prefix, self._n)
            if name not in names:
                return name

    def entries(self):
        return [st[1] for st in self.stmts if st[0] == "script"]

    def label_of_script(self, script_id):
        return self.entries()[script_id - 1]

    def _resolve(self, where):
        return self.label_of_script(where) if isinstance(where, int) else where

    def _label_index(self, label):
        for i, st in enumerate(self.stmts):
            if st == ("label", label):
                return i
        raise KeyError(label)

    def _cmd_index(self, where, n):
        """Statement index of the n-th command (0-based) after label `where` (label or script id)."""
        i = self._label_index(self._resolve(where)) + 1
        k = 0
        while i < len(self.stmts):
            if self.stmts[i][0] in ("cmd",):
                if k == n:
                    return i
                k += 1
            elif self.stmts[i][0] == "dir":
                raise ValueError("ran into data before reaching command %d" % n)
            i += 1
        raise IndexError(n)

    def commands(self, where, count=50):
        """List (index, text) of commands following a label, for inspection."""
        res, i = [], self._label_index(self._resolve(where)) + 1
        while i < len(self.stmts) and len(res) < count:
            st = self.stmts[i]
            if st[0] == "cmd":
                res.append("%s %s" % (short_name(st[1]), ", ".join(st[2])))
            elif st[0] == "dir":
                break
            i += 1
        return res

    def _tail(self):
        """Index at which to append new blocks: before the trailing end-of-file .align padding."""
        i = len(self.stmts)
        while i > 0 and self.stmts[i - 1][0] == "dir" and self.stmts[i - 1][1] == ".align":
            i -= 1
        return i

    def _append(self, stmts):
        i = self._tail()
        self.stmts[i:i] = stmts
        if i == len(self.stmts) - len(stmts) and stmts:
            self.stmts.append(("dir", ".align", ["4"]))  # keep the file 4-aligned

    def _append_block(self, label, code):
        self._append([("label", label)] + parse_text(code))

    # -- patch operations
    def add_script(self, code, label=None):
        """Append `code` as a new script and register it in the header. Returns the new script id."""
        label = label or self._new_label("S")
        hdr_end = next(i for i, st in enumerate(self.stmts) if st[0] == "endscripts")
        self.stmts.insert(hdr_end, ("script", label))
        self._append_block(label, code)
        return len(self.entries())

    def add_entry(self, label):
        """Register an existing label as a new script entry point. Returns the new script id."""
        hdr_end = next(i for i, st in enumerate(self.stmts) if st[0] == "endscripts")
        self.stmts.insert(hdr_end, ("script", label))
        return len(self.entries())

    def replace_command(self, where, n, code):
        """Replace the n-th command after `where` with one or more commands (text)."""
        i = self._cmd_index(where, n)
        self.stmts[i:i + 1] = parse_text(code)

    def insert_before(self, where, n, code):
        """Insert commands before the n-th command after `where` (n=0: right after the label)."""
        i = self._cmd_index(where, n)
        self.stmts[i:i] = parse_text(code)

    def insert_call(self, where, n, code, auto_return=True):
        """Insert `Call <new>` before the n-th command after `where`; the new block (code + Return)
        is appended to the end of the file. Returns the new label."""
        lab = self._new_label("P")
        self.insert_before(where, n, "Call %s" % lab)
        self._append_block(lab, code + ("\nReturn" if auto_return else ""))
        return lab

    def insert_jump(self, where, n, code):
        """Replace the n-th command after `where` with `GoTo <new>`; append the new block (which must
        end with its own End/GoTo/Return). Returns the new label."""
        lab = self._new_label("P")
        self.replace_command(where, n, "GoTo %s" % lab)
        self._append_block(lab, code)
        return lab

    def add_movement(self, steps, label=None):
        """Append a movement block [(cmd, count), ...] (terminator added). Returns its label."""
        label = label or self._new_label("MV")
        block = [("dir", ".align", ["4"]), ("label", label)]
        for c, n in list(steps) + [(MOVE_END, 0)]:
            block.append(("dir", ".mov", [str(c), str(n)]))
        self._append(block)
        return label

    def to_bytes(self):
        return assemble_stmts(self.stmts)

    def to_text(self):
        out = []
        for st in self.stmts:
            k = st[0]
            if k == "label":
                out.append("%s:" % st[1])
            elif k == "script":
                out.append(".script %s" % st[1])
            elif k == "endscripts":
                out.append(".endscripts" if an.header_term else ".endscripts noterm ; header has no 0xFD13 terminator")
            elif k == "cmd":
                out.append("    %s %s" % (short_name(st[1]), ", ".join(st[2])))
            else:
                out.append("    %s %s" % (st[1], ", ".join(st[2])))
        return "\n".join(out) + "\n"


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("disasm")
    d.add_argument("file")
    d.add_argument("-o", "--out")
    a = sub.add_parser("asm")
    a.add_argument("file")
    a.add_argument("-o", "--out", required=True)
    ns = ap.parse_args(argv)
    if ns.cmd == "disasm":
        text = disasm(ns.file)
        if ns.out:
            open(ns.out, "w", encoding="utf-8", newline="\n").write(text)
        else:
            sys.stdout.write(text)
    else:
        data = asm(open(ns.file, encoding="utf-8").read())
        open(ns.out, "wb").write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
