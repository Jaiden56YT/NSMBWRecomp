#!/usr/bin/env python3
"""Convert NSMBWiicompiled's PAL function_map.txt to USA Rev 2 addresses.

Each PAL address goes through Kamek's PAL->NTSC v2 offset table (make_syms.py)
and is kept only if, in the US files, it lands in executable code and looks
like a function start (previous word ends a function, or the word itself is a
typical prologue). RELs are checked at the game's US load addresses.

Usage: python3 -I tools/pal_to_us_map.py IN_MAP OUT_MAP
"""
import struct
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from make_syms import fix_ntsc_v2

ROOT = __file__.rsplit('/tools/', 1)[0]
# US load address (module header) of each REL, from the host log (PAL + 0x40)
RELS = [('d_profileNP', 0x80768500), ('d_basesNP', 0x8076D6C0), ('d_enemiesNP', 0x809A2CE0), ('d_en_bossNP', 0x80B1C960)]


def load_dol(path):
    d = open(path, 'rb').read()
    offs = struct.unpack('>18I', d[0:72]); addrs = struct.unpack('>18I', d[72:144]); sizes = struct.unpack('>18I', d[144:216])
    return [(addrs[i], d[offs[i]:offs[i] + sizes[i]]) for i in range(7) if sizes[i]]


def load_rel(path, base):
    d = open(path, 'rb').read()
    nsec, secoff = struct.unpack('>II', d[0x0C:0x14])
    out = []
    for i in range(nsec):
        off, size = struct.unpack('>II', d[secoff + 8 * i:secoff + 8 * i + 8])
        if off & 1 and size:
            o = off & ~3
            out.append((base + o, d[o:o + size]))
    return out


TEXT = load_dol(f'{ROOT}/code/main.dol')
for name, base in RELS:
    TEXT += load_rel(f'{ROOT}/code/rels/{name}.rel', base)


def word(a):
    for start, data in TEXT:
        if start <= a < start + len(data) and a + 4 <= start + len(data):
            return struct.unpack('>I', data[a - start:a - start + 4])[0], a == start
    return None, False


def looks_like_start(a):
    w, first = word(a)
    if w is None: return False
    if first: return True
    if (w >> 16) == 0x9421: return True          # stwu r1,-N(r1)
    p, _ = word(a - 4)
    if p is None: return True
    if p in (0x4E800020, 0x4C000064, 0, 0x4E800420): return True  # blr, rfi, padding, bctr
    if (p >> 26) == 18 and not (p & 1): return True              # b (tail call)
    return False


def rel_pointer_targets():
    """Function pointers in the RELs' data, from their relocation tables
    (R_PPC_ADDR32 relocations whose target is code), at US load addresses."""
    found = set()
    ids = {}
    for name, base in RELS:
        d = open(f'{ROOT}/code/rels/{name}.rel', 'rb').read()
        ids[struct.unpack('>I', d[0:4])[0]] = (name, base, d)
    def sec_info(d, base, sec):
        nsec, secoff = struct.unpack('>II', d[0x0C:0x14])
        off, size = struct.unpack('>II', d[secoff + 8 * sec:secoff + 8 * sec + 8])
        return base + (off & ~3), bool(off & 1)
    for mid, (name, base, d) in ids.items():
        imp_off, imp_size = struct.unpack('>II', d[0x28:0x30])
        for k in range(imp_size // 8):
            target_mod, rel_off = struct.unpack('>II', d[imp_off + 8 * k:imp_off + 8 * k + 8])
            p = rel_off
            src_exec = False
            while True:
                off, typ, sec, addend = struct.unpack('>HBBI', d[p:p + 8])
                p += 8
                if typ == 203: break
                if typ == 202:
                    _, src_exec = sec_info(d, base, sec)
                    continue
                if typ != 1 or src_exec:  # ADDR32 from a data section only
                    continue
                if target_mod == 0:
                    found.add(addend)                      # pointer into main.dol
                elif target_mod in ids:
                    tname, tbase, td = ids[target_mod]
                    start, ex = sec_info(td, tbase, sec)
                    if ex: found.add(start + addend)
    return found


def dol_pointer_targets():
    """Words in main.dol's data sections that point at a function start in its code."""
    d = open(f'{ROOT}/code/main.dol', 'rb').read()
    offs = struct.unpack('>18I', d[0:72]); addrs = struct.unpack('>18I', d[72:144]); sizes = struct.unpack('>18I', d[144:216])
    found = set()
    for i in range(7, 18):
        seg = d[offs[i]:offs[i] + sizes[i]]
        for k in range(0, len(seg) - 3, 4):
            w = struct.unpack('>I', seg[k:k + 4])[0]
            if 0x80004000 <= w < 0x80400000 and not (w & 3): found.add(w)
    return found


SAVE_RESTORE = ('_savegpr_', '_restgpr_', '_savefpr_', '_restfpr_')


def main():
    src, dst = sys.argv[1], sys.argv[2]
    kept = dropped = 0
    out = []
    have = set()
    for line in open(src):
        parts = line.split()
        if not parts or line.startswith('#'):
            continue
        try:
            a = int(parts[0], 16)
        except ValueError:
            continue
        u = fix_ntsc_v2(a)
        name = parts[1] if len(parts) > 1 else ''
        if looks_like_start(u) or (name.startswith(SAVE_RESTORE) and word(u)[0] is not None):
            out.append(f'{u:08X} ' + ' '.join(parts[1:]))
            have.add(u)
            kept += 1
        else:
            dropped += 1
    added = 0
    for t in sorted(rel_pointer_targets() | dol_pointer_targets()):
        if t not in have and looks_like_start(t) and word(t)[0] is not None:
            out.append(f'{t:08X} ptr_target_{t:08X}')
            have.add(t)
            added += 1
    open(dst, 'w').write('\n'.join(sorted(set(out))) + '\n')
    print(f'kept {kept}, dropped {dropped}, added {added} pointer targets', file=sys.stderr)


if __name__ == '__main__':
    main()
