#!/usr/bin/env python3
"""Copy HeartGold's follower Pokemon sprites into Gunk's Diamond (read-only on the ROM).

usage: python3 -I tools/gunk/followers/extract_hg.py <HeartGold (USA) ROM> [first last]

HeartGold keeps a walking overworld sprite for every Pokemon (and its forms) in a/0/8/1 (data/mmodel/mmodel.narc),
members 297-862, plus the walk frame sequence at 292. They are the same NSBTX billboard format Diamond's
mmodel.narc uses, so they are copied byte for byte:
  HG member 292           -> Diamond mmodel member 414 (narc_0414.bin, walk frame sequence)
  HG member 297 + n       -> Diamond mmodel member 415 + n (narc_<415+n>.NSBTX), n = HG follower index
Optional first/last limit the sprite range (HG follower indices) while the port is being tested.
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
OUT = os.path.join(ROOT, 'files', 'data', 'mmodel', 'mmodel')

HG_SHA1 = '4fcded0e2713dc03929845de631d0932ea2b5a37'  # Pokemon - HeartGold Version (USA)
HG_FIRST_SPRITE = 297
HG_SPRITES = 566
HG_WALK_SEQ = 292
DP_WALK_SEQ = 414
DP_FIRST_SPRITE = 415


def rom_file(rom, path):
    """the bytes of a file in the ROM's NitroFS, by path (a/0/8/1)"""
    fnt_off, fnt_size, fat_off, fat_size = struct.unpack_from('<IIII', rom, 0x40)
    fnt = rom[fnt_off:fnt_off + fnt_size]

    def lookup(dir_id, names):
        off, first, _ = struct.unpack_from('<IHH', fnt, (dir_id & 0xFFF) * 8)
        p, fid = off, first
        while True:
            t = fnt[p]
            p += 1
            if t == 0:
                raise KeyError('/'.join(names))
            n = t & 0x7F
            name = fnt[p:p + n].decode()
            p += n
            if t & 0x80:
                sub = struct.unpack_from('<H', fnt, p)[0]
                p += 2
                if name == names[0]:
                    return lookup(sub, names[1:])
            else:
                if len(names) == 1 and name == names[0]:
                    return fid
                fid += 1

    fid = lookup(0xF000, path.split('/'))
    start, end = struct.unpack_from('<II', rom, fat_off + fid * 8)
    return rom[start:end]


def narc_members(narc):
    """[bytes] of a NARC's members"""
    assert narc[:4] == b'NARC'
    p = struct.unpack_from('<H', narc, 0xC)[0]
    assert narc[p:p + 4] == b'BTAF'
    btaf_size, count = struct.unpack_from('<IH', narc, p + 4)
    entries = [struct.unpack_from('<II', narc, p + 12 + i * 8) for i in range(count)]
    p += btaf_size
    assert narc[p:p + 4] == b'BTNF'
    p += struct.unpack_from('<I', narc, p + 4)[0]
    assert narc[p:p + 4] == b'GMIF'
    data = p + 8
    return [narc[data + a:data + b] for a, b in entries]


def main():
    import hashlib
    rom_path = sys.argv[1]
    first, last = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (0, HG_SPRITES - 1)
    rom = open(rom_path, 'rb').read()
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != HG_SHA1:
        sys.exit(f'not Pokemon HeartGold (USA): sha1 {sha1}')
    members = narc_members(rom_file(rom, 'a/0/8/1'))
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, f'narc_{DP_WALK_SEQ:04d}.bin'), 'wb').write(members[HG_WALK_SEQ])
    for n in range(first, last + 1):
        sprite = members[HG_FIRST_SPRITE + n]
        assert sprite[:4] == b'BTX0', f'HG member {HG_FIRST_SPRITE + n} is not a texture'
        open(os.path.join(OUT, f'narc_{DP_FIRST_SPRITE + n:04d}.NSBTX'), 'wb').write(sprite)
    print(f'copied the walk sequence and HG follower sprites {first}-{last} to {OUT}')


if __name__ == '__main__':
    main()
