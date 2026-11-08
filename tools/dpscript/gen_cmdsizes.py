#!/usr/bin/env python3
"""Derive the argument layout of every field-script opcode from the pokediamond sources.

Usage:  python gen_cmdsizes.py [path/to/pokemon-pearl-gunk]  ->  writes cmdsizes.py next to this file

Argument type codes (all little-endian):
    b  u8                      h  u16 (immediate)
    v  u16 var-or-value (ScriptGetVar: >=0x4000 reads a var)
    p  u16 var id (ScriptGetVarPointer: written to)
    w  u32 (immediate)         r  s32 relative offset (target = end of the field + value) to script code
    m  s32 relative offset to a movement block (ApplyMovement)

The derivation walks each ScrCmd_* body in source order and records calls to
ScriptReadByte / ScriptReadHalfword / ScriptReadWord / ScriptGetVar / ScriptGetVarPointer,
inlining any helper (C or asm) that is passed `ctx` and itself reads script bytes.
A ScriptReadWord whose result is added to ctx->scriptPtr becomes `r`.
Reads under if/switch/loop (C) or after a conditional branch / label (asm) are flagged
`conditional`; every flagged command must be resolved by an entry in OVERRIDES below.
"""
import os
import re
import sys
import pprint

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REPO = r"C:\Projects\pokemon-pearl-gunk"

# ---------------------------------------------------------------------------
# Manual overrides.  opcode -> (layout, reason)
# layout is a list of type codes, or the string "special:<kind>" for commands that
# need custom decoding in dpscript.py.
# ---------------------------------------------------------------------------
#
# A "switch" layout is a dict: {"switch": <selector type>, "cases": {value: [types]}, "default": [types]}
# meaning: read the selector, then the arguments listed for that selector value.
def _sw(sel, cases, default=()):
    return {"switch": sel, "cases": cases, "default": list(default)}


_GETSET = _sw("b", {0: [], 1: [], 2: ["p"]})

OVERRIDES = {
    0x005E: (["v", "m"],
             "ApplyMovement: the u32 is a relative pointer to movement data (passed to sub_0205AEA4), not code"),
    0x01CF: (_GETSET, "GetSetStrength: u8 option; only option 2 (get) reads a result var"),
    0x01D0: (_GETSET, "GetSetFlash: u8 option; only option 2 (get) reads a result var"),
    0x01D1: (_GETSET, "GetSetDefog: u8 option; only option 2 (get) reads a result var"),
    0x021D: (_sw("h", {0: ["v", "p"], 1: ["v", "p"], 2: ["v", "v"], 3: ["v", "v"], 4: ["p"], 5: ["v"], 6: []}),
             "UnionGroup: u16 option selects a sub-command with its own args (scrcmd_union.c switch)"),
    0x0235: (_sw("h", {0: ["p"], 1: ["h", "p", "p"], 2: [], 3: ["v", "p", "p"], 4: ["p", "p"],
                      5: ["v", "v", "p"], 6: ["p"]}),
             "Unk0235 (asm, scrcmd_9.s): u16 selector -> jump table with per-case reads"),
    0x0237: (_sw("h", {0: ["v", "p", "p"], 1: ["v", "v", "v"]}),
             "Unk0237 (asm, scrcmd_9.s): u16 selector 0/1 with different args; other values read nothing"),
    0x023E: (_sw("h", {0: [], 1: ["p"], 2: ["p"], 3: ["p"], 4: [], 5: ["p", "p"], 6: ["p", "p"], 7: [], 8: []}),
             "Unk023E (asm, scrcmd_12.s): u16 selector -> jump table with per-case reads"),
    0x027C: (_sw("h", {0: ["v"], 1: ["v"], 2: ["v"]}),
             "Unk027C (asm, scrcmd_9.s, Safari): u16 selector 0..2 each read one var; others read nothing"),
    0x0289: (["p", "b", "b", "b", "b", "b", "b"],
             "Unk0289 (poffin): reads 5 bytes in a for-loop, then one more byte"),
}

# Commands whose reads are inside a conditional block but are known to be read on
# every path (or where the conditional read is the documented layout).  opcode -> reason
CONDITIONAL_OK = {}

# Commands that end straight-line execution (no fall-through).
TERMINATORS = {"ScrCmd_End", "ScrCmd_GoTo", "ScrCmd_Return"}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read()


# ---------------------------------------------------------------- C parsing
def strip_c(src):
    """Remove comments/strings and resolve `#ifdef NONMATCHING ... #else ... #endif` to the C branch."""
    src = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r'"(?:\\.|[^"\\])*"', '""', src)
    src = re.sub(r"'(?:\\.|[^'\\])'", "0", src)
    out, stack = [], []  # stack of booleans: keep?
    for line in src.split("\n"):
        s = line.strip()
        if s.startswith("#if"):
            stack.append(True)  # keep first branch of every #if (NONMATCHING => C version)
            out.append("")
            continue
        if s.startswith("#else") or s.startswith("#elif"):
            if stack:
                stack[-1] = False
            out.append("")
            continue
        if s.startswith("#endif"):
            if stack:
                stack.pop()
            out.append("")
            continue
        out.append(line if all(stack) else "")
    return "\n".join(out)


FUNC_RE = re.compile(r"\b(\w+)\s*\(\s*(?:struct\s+)?ScriptContext\s*\*\s*(\w+)\s*\)\s*\{")


def match_brace(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "{":
            depth += 1
        elif s[j] == "}":
            depth -= 1
            if depth == 0:
                return j
    raise ValueError("unbalanced")


def match_paren(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    raise ValueError("unbalanced")


def conditional_spans(body):
    """Return list of (start, end) char spans of body that execute conditionally."""
    spans = []
    for m in re.finditer(r"\b(if|while|for|switch|else|do)\b|\?", body):
        kw = m.group(1)
        if m.group(0) == "?":
            end = body.find(";", m.end())
            spans.append((m.end(), end))
            continue
        j = m.end()
        if kw in ("if", "while", "for", "switch"):
            p = body.find("(", j)
            pe = match_paren(body, p)
            if kw in ("while", "for"):
                spans.append((p, pe))  # loop conditions may execute repeatedly
            j = pe + 1
        while j < len(body) and body[j].isspace():
            j += 1
        if kw == "else" and body.startswith("if", j):
            continue  # handled by the following `if`
        if j < len(body) and body[j] == "{":
            spans.append((j, match_brace(body, j)))
        else:
            spans.append((j, body.find(";", j)))
    return spans


READ_RE = re.compile(r"\b(ScriptReadByte|ScriptReadHalfword|ScriptReadWord|ScriptGetVarPointer|ScriptGetVar)\s*\(\s*CTX\s*\)")
READ_TYPES = {"ScriptReadByte": "b", "ScriptReadHalfword": "h", "ScriptReadWord": "w",
              "ScriptGetVar": "v", "ScriptGetVarPointer": "p"}


def parse_c_functions(paths):
    funcs = {}
    for path in paths:
        src = strip_c(read(path))
        for m in FUNC_RE.finditer(src):
            name, ctxname = m.group(1), m.group(2)
            if name in ("if", "while", "for", "switch"):
                continue
            b0 = m.end() - 1
            b1 = match_brace(src, b0)
            funcs[name] = dict(lang="c", file=os.path.basename(path), body=src[b0:b1 + 1], ctx=ctxname)
        # also catch `asm BOOL f(ScriptContext *ctx) {` (handled above since the regex is name-based)
    return funcs


def analyze_c(name, funcs, seen):
    f = funcs[name]
    body, ctx = f["body"], f["ctx"]
    rx = re.compile(READ_RE.pattern.replace("CTX", re.escape(ctx)))
    spans = conditional_spans(body)
    reads, notes = [], []
    events = [(m.start(), m) for m in rx.finditer(body)]
    # bare `ctx` passed as an argument to some other function -> possible helper that reads
    for m in re.finditer(r"\b%s\b(?!\s*->)" % re.escape(ctx), body):
        # find the enclosing call
        depth, j = 0, m.start() - 1
        while j >= 0:
            if body[j] == ")":
                depth += 1
            elif body[j] == "(":
                if depth == 0:
                    break
                depth -= 1
            j -= 1
        if j < 0:
            continue
        cm = re.search(r"(\w+)\s*$", body[:j])
        if cm and cm.group(1) not in READ_TYPES:
            events.append((m.start(), ("call", cm.group(1))))
    events.sort(key=lambda e: e[0])
    for start, m in events:
        cond = any(a <= start <= b for a, b in spans)
        if isinstance(m, tuple):
            callee = m[1]
            if callee in ("ScriptJump", "ScriptCall", "SetupNativeScript", "StopScript", "ScriptReturn",
                          "if", "while", "for", "switch", "sizeof"):
                continue
            if callee in funcs and callee not in seen:
                sub, subnotes = analyze(callee, funcs, seen | {name})
                if sub:
                    reads.extend((t, c or cond) for t, c in sub)
                    notes.append("inlined " + callee)
                notes.extend(subnotes)
            continue
        if m.group(1):
            t = READ_TYPES[m.group(1)]
            if t == "w":
                # find assigned variable and check for scriptPtr + var
                pre = body[max(0, m.start() - 80):m.start()]
                vm = re.search(r"(\w+)\s*=\s*(?:\(\s*\w+\s*\)\s*)?$", pre)
                if vm:
                    var = vm.group(1)
                    if re.search(r"scriptPtr\s*(?:\+=|\+)\s*(?:\(\s*\w+\s*\)\s*)?" + re.escape(var) + r"\b", body):
                        t = "r"
            reads.append((t, cond))
    # native callbacks registered with SetupNativeScript run later and may read too
    for m in re.finditer(r"SetupNativeScript\s*\(\s*\w+\s*,\s*(\w+)\s*\)", body):
        cb = m.group(1)
        if cb in funcs and cb not in seen:
            sub, _ = analyze(cb, funcs, seen | {name})
            if sub:
                reads.extend((t, True) for t, c in sub)
                notes.append("native callback %s reads script bytes" % cb)
    return reads, notes


# ---------------------------------------------------------------- asm parsing
COND_BRANCH = re.compile(r"^\s*b(eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)\b")


def parse_asm_functions(paths):
    funcs = {}
    for path in paths:
        name, lines = None, []
        for line in read(path).split("\n"):
            m = re.match(r"\s*(?:thumb|arm)_func_start\s+(\w+)", line)
            if m:
                if name:
                    funcs[name] = dict(lang="asm", file=os.path.basename(path), lines=lines)
                name, lines = m.group(1), []
                continue
            if name:
                lines.append(line.split(";")[0].rstrip() if not line.lstrip().startswith(".word") else line)
        if name:
            funcs[name] = dict(lang="asm", file=os.path.basename(path), lines=lines)
    # the inline-asm version of ScrCmd_CheckBurmyForms is not needed (C version is used)
    return funcs


def analyze_asm(name, funcs, seen):
    lines = funcs[name]["lines"]
    reads, notes = [], []
    cond = False
    for i, line in enumerate(lines):
        s = line.strip()
        if COND_BRANCH.match(s) or re.match(r"^_\w+:", s) or s.startswith("bl _") or re.match(r"^b\s+_", s):
            if re.match(r"^_\w+:", s) or COND_BRANCH.match(s):
                cond = True
        m = re.match(r"bl\s+(\w+)", s)
        if m:
            callee = m.group(1)
            if callee == "ScriptReadHalfword":
                t = "h"
                for nxt in lines[i + 1:i + 12]:
                    mm = re.match(r"\s*bl\s+(\w+)", nxt)
                    if mm:
                        if mm.group(1) == "FieldSystem_VarGet":
                            t = "v"
                        elif mm.group(1) == "GetVarPointer":
                            t = "p"
                        break
                reads.append((t, cond))
            elif callee == "ScriptReadWord":
                reads.append(("w", cond))
            elif callee in funcs and callee not in seen and funcs[callee]["lang"] == "asm":
                sub, _ = analyze(callee, funcs, seen | {name})
                if sub:
                    reads.extend((t, c or cond) for t, c in sub)
                    notes.append("inlined " + callee)
            continue
        # inlined ScriptReadByte: ldr rA,[rCtx,#0x8] ; add rB,rA,#0x1 ; str rB,[rCtx,#0x8] ; ldrb ..,[rA]
        # (two consecutive inlined reads reuse the incremented pointer: add r1,r2,#1 ; str r1,[r5,#8])
        # => count every store of (reg + 1) into ctx->scriptPtr (offset 0x8, non-sp base).
        m = re.match(r"str\s+(r\d),\s*\[(r\d),\s*#0x8\]", s)
        if m:
            rb = m.group(1)
            for prev in reversed(lines[max(0, i - 6):i]):
                pm = re.match(r"\s*(\w+)\s+(r\d)\b(.*)", prev)
                if pm and pm.group(2) == rb:
                    if pm.group(1) == "add" and re.match(r",\s*r\d,\s*#0x1$", pm.group(3).strip()):
                        reads.append(("b", cond))
                    break
    # `.word sub_xxx` literal => native callback; check it for reads
    for line in lines:
        mm = re.search(r"\.word\s+(\w+)", line)
        if mm and mm.group(1) in funcs and mm.group(1) not in seen:
            sub, _ = analyze(mm.group(1), funcs, seen | {name})
            if sub:
                reads.extend((t, True) for t, c in sub)
                notes.append("callback %s reads script bytes" % mm.group(1))
    return reads, notes


def analyze(name, funcs, seen=frozenset()):
    f = funcs[name]
    if f["lang"] == "c":
        return analyze_c(name, funcs, seen | {name})
    return analyze_asm(name, funcs, seen | {name})


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_REPO
    table_src = read(os.path.join(repo, "arm9", "asm", "unk_02038C78.s"))
    t = table_src[table_src.index("gScriptCmdTable:"):]
    names = []
    for line in t.split("\n")[1:]:
        m = re.match(r"\s*\.word\s+(\w+)", line)
        if not m:
            if names and line.strip() and not line.strip().startswith(";"):
                break
            continue
        names.append(m.group(1))
    assert len(names) == 721, len(names)

    src_dir = os.path.join(repo, "arm9", "src")
    asm_dir = os.path.join(repo, "arm9", "asm")
    cfiles = [os.path.join(src_dir, f) for f in sorted(os.listdir(src_dir)) if f.startswith("scrcmd") and f.endswith(".c")]
    sfiles = [os.path.join(asm_dir, f) for f in sorted(os.listdir(asm_dir)) if f.startswith("scrcmd") and f.endswith(".s")]
    funcs = parse_asm_functions(sfiles)
    funcs.update(parse_c_functions(cfiles))  # C wins over asm (NONMATCHING C bodies)

    cmds, problems = {}, []
    for op, name in enumerate(names):
        if name not in funcs:
            problems.append((op, name, "implementation not found"))
            cmds[op] = dict(name=name, args=None, src=None, conditional=False, notes=["NOT FOUND"])
            continue
        reads, notes = analyze(name, funcs)
        cond = any(c for _, c in reads)
        args = [t for t, _ in reads]
        entry = dict(name=name, args=args, src=funcs[name]["file"], conditional=cond, notes=notes)
        if op in OVERRIDES:
            layout, reason = OVERRIDES[op]
            entry["derived"] = args
            entry["args"] = layout
            entry["override"] = reason
        elif cond and op not in CONDITIONAL_OK:
            problems.append((op, name, "conditional read(s) %s; needs override" % "".join(args)))
        elif cond:
            entry["override"] = CONDITIONAL_OK[op]
        cmds[op] = entry

    out = os.path.join(HERE, "cmdsizes.py")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# AUTO-GENERATED by gen_cmdsizes.py -- do not edit; edit OVERRIDES in gen_cmdsizes.py\n")
        f.write("# opcode -> dict(name, args, src, conditional, notes[, override, derived])\n")
        f.write("TERMINATORS = %r\n" % sorted(TERMINATORS))
        f.write("CMDS = ")
        f.write(pprint.pformat(cmds, width=120, sort_dicts=False))
        f.write("\n")
    print("wrote %s (%d opcodes)" % (out, len(cmds)))
    if problems:
        print("%d unresolved:" % len(problems))
        for op, name, why in problems:
            print("  0x%04X %-32s %s  [%s]" % (op, name, why, cmds[op].get("src")))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
