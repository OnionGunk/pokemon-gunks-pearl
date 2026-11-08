#!/usr/bin/env python3
"""Gunk's stand-in for the Nitro SDK's makerom.exe (pokediamond's rom.rsf only).

usage: makerom.py -DNAME=VALUE ... rom.rsf out.nds

Lays the ROM out the way makerom does for Diamond/Pearl: 0x4000 header, ARM9 static, ARM9 overlay table and
overlay files, ARM7 static, FNT, FAT, banner, then the NitroFS files in the rsf's order, every block 0x200-aligned
and padded with 0xFF, and the image padded to the full RomSize. The header comes from the template
(tools/bin/rom_header.template.sbin, made from the base ROM by tools/gunk/setup_sdk.sh) with every layout field
recomputed.
"""
import re
import struct
import sys

ALIGN = 0x200


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def parse_rsf(text, defs):
    def sub(m):
        return defs.get(m.group(1), '')
    text = re.sub(r'\$\((\w+)\)', sub, text)
    blocks = {}
    for name, body in re.findall(r'(\w+)\s*\{([^}]*)\}', text):
        entries = []
        for line in body.splitlines():
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            key, _, val = line.partition(' ')
            entries.append((key, val.strip()))
        blocks[name] = entries
    return blocks


def get(block, key):
    for k, v in block:
        if k == key:
            return v
    return None


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def split_static(data):
    """Static binaries end with the 12-byte NITRO footer; makerom writes it but leaves it out of the size."""
    if len(data) >= 12 and struct.unpack_from('<I', data, len(data) - 12)[0] == 0xDEC00621:
        return data[:-12], data[-12:]
    return data, b''


def parse_defs(path):
    d = read(path)
    ram, entry, size, autoload_cb = struct.unpack_from('<4I', d, 0)
    names = [n.decode() for n in d[16:].split(b'\0') if n]
    return ram, entry, autoload_cb, names


class Rom:
    def __init__(self):
        self.buf = bytearray()

    def align(self):
        pad = (-len(self.buf)) % ALIGN
        self.buf += b'\xff' * pad

    def put(self, data):
        self.align()
        off = len(self.buf)
        self.buf += data
        return off


def build_fnt(files):
    """files: list of paths (rsf order). Returns (fnt bytes, {path: file id}) with ids starting at first_id."""
    tree = {}
    for p in files:
        node = tree
        parts = p.split('/')
        for d in parts[:-1]:
            node = node.setdefault(d + '/', {})
        node[parts[-1]] = None
        dirs = []  # (node, parent index)

    def order(node):
        return sorted(node.keys(), key=lambda k: k.rstrip('/').lower())
    def visit(node, parent):  # directory ids: depth-first preorder in sorted order, as makerom numbers them
        dirs.append((node, parent))
        idx = len(dirs) - 1
        for k in order(node):
            if k.endswith('/'):
                visit(node[k], idx)
    visit(tree, 0)
    return tree, dirs, order


def make_fnt(files, first_id):
    tree, dirs, order = build_fnt(files)
    dir_index = {id(n): i for i, (n, _) in enumerate(dirs)}
    ids = {}
    # file ids: depth-first in sorted order, matching makerom
    next_id = [first_id]
    first_of = {}

    def assign(node, prefix):
        first_of[id(node)] = next_id[0]
        for k in order(node):
            if not k.endswith('/'):
                ids[prefix + k] = next_id[0]
                next_id[0] += 1
        for k in order(node):
            if k.endswith('/'):
                assign(node[k], prefix + k)
    assign(tree, '')
    subtables = []
    for node, parent in dirs:
        st = bytearray()
        # files first, then subdirectories, each in sorted order
        for k in [k for k in order(node) if not k.endswith('/')] + [k for k in order(node) if k.endswith('/')]:
            name = k.rstrip('/').encode()
            if k.endswith('/'):
                st += bytes([0x80 | len(name)]) + name + struct.pack('<H', 0xF000 | dir_index[id(node[k])])
            else:
                st += bytes([len(name)]) + name
        st += b'\0'
        subtables.append(bytes(st))
    main = bytearray()
    off = 8 * len(dirs)
    for i, (node, parent) in enumerate(dirs):
        par = len(dirs) if i == 0 else 0xF000 | parent
        main += struct.pack('<IHH', off, first_of[id(node)], par)
        off += len(subtables[i])
    return bytes(main) + b''.join(subtables), ids


def main():
    defs = {}
    args = []
    for a in sys.argv[1:]:
        if a.startswith('-D'):
            k, _, v = a[2:].partition('=')
            defs[k] = v
        else:
            args.append(a)
    rsf_path, out_path = args
    blocks = parse_rsf(open(rsf_path).read(), defs)
    arm9, arm7, prop, spec = blocks['Arm9'], blocks['Arm7'], blocks['Property'], blocks['RomSpec']

    header = bytearray(read(get(prop, 'RomHeaderTemplate'))[:0x4000])
    header += b'\0' * (0x4000 - len(header))
    title = get(prop, 'TitleName').strip('"').encode()[:12]
    header[0:12] = title + b'\0' * (12 - len(title))
    header[0x10:0x12] = get(prop, 'MakerCode').strip('"').encode()
    header[0x1E] = int(get(prop, 'RemasterVersion'))

    rom = Rom()
    rom.buf += header

    a9, a9foot = split_static(read(get(arm9, 'Static')))
    ram9, entry9, cb9, ov9names = parse_defs(get(arm9, 'OverlayDefs'))
    a9off = rom.put(a9 + a9foot)
    ov9table = bytearray(read(get(arm9, 'OverlayTable')))
    ov9names_dir = get(arm9, 'Static').rsplit('/', 1)[0]
    # overlay file ids follow table order
    for i in range(len(ov9table) // 32):
        struct.pack_into('<I', ov9table, i * 32 + 0x18, i)
    ov9off = rom.put(bytes(ov9table)) if ov9table else 0
    fat = []
    for name in ov9names:
        data = read(ov9names_dir + '/' + name)
        off = rom.put(data)
        fat.append((off, off + len(data)))

    a7, a7foot = split_static(read(get(arm7, 'Static')))
    ram7, entry7, cb7, ov7names = parse_defs(get(arm7, 'OverlayDefs'))
    a7off = rom.put(a7 + a7foot)

    root = get(spec, 'HostRoot')
    files = get(spec, 'File').split()
    fnt, ids = make_fnt(files, len(fat))
    fntoff = rom.put(fnt)
    nfiles = len(fat) + len(files)
    fatoff_placeholder = rom.put(b'\0' * (8 * nfiles))
    banner = read(get(prop, 'BannerFile'))
    banneroff = rom.put(banner)
    fat += [None] * len(files)
    for p in files:
        data = read(root + p)
        off = rom.put(data)
        fat[ids[p]] = (off, off + len(data))
    used = len(rom.buf)
    fatbytes = b''.join(struct.pack('<II', a, b) for a, b in fat)
    rom.buf[fatoff_placeholder:fatoff_placeholder + len(fatbytes)] = fatbytes

    h = rom.buf
    struct.pack_into('<4I', h, 0x20, a9off, entry9, ram9, len(a9))
    struct.pack_into('<4I', h, 0x30, a7off, entry7, ram7, len(a7))
    struct.pack_into('<4I', h, 0x40, fntoff, len(fnt), fatoff_placeholder, len(fatbytes))
    struct.pack_into('<4I', h, 0x50, ov9off, len(ov9table), 0, 0)
    struct.pack_into('<I', h, 0x68, banneroff)
    struct.pack_into('<I', h, 0x70, cb9)
    struct.pack_into('<I', h, 0x74, cb7)
    struct.pack_into('<II', h, 0x80, used, 0x4000)

    size = {'64M': 8, '128M': 16, '256M': 32, '512M': 64, '1G': 128, '2G': 256}[get(prop, 'RomSize')] << 20
    if get(prop, 'RomFootPadding') == 'TRUE':
        h += b'\xff' * (size - len(h))
    struct.pack_into('<H', h, 0x15E, crc16(h[:0x15E]))
    with open(out_path, 'wb') as f:
        f.write(h)


if __name__ == '__main__':
    main()
