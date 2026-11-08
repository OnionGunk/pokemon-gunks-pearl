#!/usr/bin/env python3
"""Disassemble + reassemble every scr_seq_release file and check byte-identical output.

Usage: python roundtrip.py [script_dir] [-v] [--dump OUTDIR]
Reports: identical/total, files with walker warnings (desyncs), and coverage
(header / command / movement / unclassified-data bytes).
"""
import collections
import glob
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dpscript  # noqa: E402

DEFAULT_DIR = r"C:\Projects\pokemon-pearl-gunk\files\fielddata\script\scr_seq_release"


def main(argv):
    verbose = "-v" in argv
    dump = None
    if "--dump" in argv:
        dump = argv[argv.index("--dump") + 1]
        os.makedirs(dump, exist_ok=True)
    pos = [a for a in argv if not a.startswith("-") and a != dump]
    sdir = pos[0] if pos else DEFAULT_DIR
    files = sorted(glob.glob(os.path.join(sdir, "narc_*.bin")))
    ok, bad, warn_files = 0, [], []
    tot = collections.Counter()
    warn_kinds = collections.Counter()
    data_files = []
    kinds = collections.Counter()
    for path in files:
        data = open(path, "rb").read()
        name = os.path.basename(path)
        try:
            kind = dpscript.kind_for_path(path) if data else "empty"
            kinds[kind] += 1
            text = dpscript.disasm(data, kind if data else None)
            out = dpscript.asm(text)
        except Exception as ex:  # noqa: BLE001
            bad.append((name, "exception: %r" % ex))
            continue
        if dump:
            open(os.path.join(dump, name[:-4] + ".s"), "w", encoding="utf-8", newline="\n").write(text)
        if out == data:
            ok += 1
        else:
            first = next((i for i in range(min(len(out), len(data))) if out[i] != data[i]), min(len(out), len(data)))
            bad.append((name, "mismatch at 0x%X (len %d vs %d)" % (first, len(out), len(data))))
        if kind != "script":
            cov = dict(total=len(data))
            if kind == "level":
                end = dpscript.parse_level(data)[2]
                cov.update(level=end, pad=len(data) - end)
            tot.update(cov)
            continue
        an = dpscript.Analysis(data)
        cov = an.coverage()
        tot.update(cov)
        if an.errors:
            warn_files.append((name, an.errors))
            for e in an.errors:
                warn_kinds[e.split(" at ")[0].split(" 0x")[0]] += 1
        if cov["data"]:
            data_files.append((name, cov["data"]))
    print("files: %d (%s)   round-trip identical: %d/%d" % (
        len(files), ", ".join("%s %d" % kv for kv in sorted(kinds.items())), ok, len(files)))
    for name, why in bad:
        print("  FAIL %s: %s" % (name, why))
    print("walker warnings (desync/unknown opcode/overlap): %d files" % len(warn_files))
    for k, n in warn_kinds.most_common():
        print("  %5d  %s" % (n, k))
    if verbose:
        for name, errs in warn_files:
            for e in errs:
                print("  %s: %s" % (name, e))
    t = tot["total"]
    print("bytes: total %d | header %d (%.2f%%) | commands %d (%.2f%%) | movement %d (%.2f%%) | "
          "unclassified data %d (%.2f%%)" % (t, tot["header"], 100 * tot["header"] / t, tot["cmd"], 100 * tot["cmd"] / t,
                                             tot["mov"], 100 * tot["mov"] / t, tot["data"], 100 * tot["data"] / t))
    print("  (of the command/movement bytes, %d were reached only by speculative decoding of unreferenced blocks)" % tot["spec"])
    print("level-script files: %d structured bytes, %d trailing padding" % (tot["level"], tot["pad"]))
    print("script files: unclassified data %d bytes, of which %d are 0x00 (alignment padding)" % (
        tot["data"], tot["zero"]))
    print("files with unclassified data: %d" % len(data_files))
    if verbose:
        for name, n in sorted(data_files, key=lambda x: -x[1])[:40]:
            print("  %s: %d bytes" % (name, n))
    return 0 if not bad and not warn_files else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
