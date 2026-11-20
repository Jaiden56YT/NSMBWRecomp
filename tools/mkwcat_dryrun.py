"""Dry run for mkwcat's nsmbw-project on the NSMBW recomp (US Rev 2, SMNE01).

Usage: python3 -I mkwcat_dryrun.py <mkwcat-nsmbw.arc> <code/rels dir> <ports/wiicompiled dir> [<module base hex>]

Unpacks rels/d_project.rel.szs and rels/d_project_E2.imp.szs from the pack, splices the import table
in as the mod's loader does (at the REL's impOffset), links the module at a fixed base against the
recomp's main.dol / REL layout, and recovers the tables its _prolog applies:
  replace_array (game function -> mod function: a `b` written at the game function),
  patch_references_array (16-bit immediates in game code pointed at the mod's arrays),
  extern_array (call-original thunks), FOUR_PATCH_LIST (player-count immediates).
Prints counts and samples; writes nothing. Reads only the files given.
"""
import struct
import sys
import re
import os


def u8_files(data):
    assert data[:4] == b"\x55\xAA\x38\x2D", "not a U8 archive"
    root, header_size, data_off = struct.unpack_from(">III", data, 4)
    n = struct.unpack_from(">I", data, root + 8)[0]
    strings = root + n * 12
    out = {}
    path_stack = []  # (end_index, name)

    def name_at(off):
        e = data.index(b"\0", strings + off)
        return data[strings + off:e].decode("latin1")

    for i in range(1, n):
        while path_stack and path_stack[-1][0] <= i:
            path_stack.pop()
        typ = data[root + i * 12]
        name_off = struct.unpack_from(">I", data, root + i * 12)[0] & 0xFFFFFF
        a, b = struct.unpack_from(">II", data, root + i * 12 + 4)
        name = name_at(name_off)
        if typ == 1:
            path_stack.append((b, name))
        else:
            path = "/".join([p[1] for p in path_stack] + [name])
            out[path] = data[a:a + b]
    return out


def yaz0(src):
    if src[:4] != b"Yaz0":
        return src
    size = struct.unpack_from(">I", src, 4)[0]
    dst = bytearray()
    s = 16
    while len(dst) < size:
        code = src[s]
        s += 1
        for bit in range(7, -1, -1):
            if len(dst) >= size:
                break
            if code & (1 << bit):
                dst.append(src[s])
                s += 1
            else:
                b1, b2 = src[s], src[s + 1]
                s += 2
                dist = ((b1 & 0xF) << 8 | b2) + 1
                n = b1 >> 4
                if n == 0:
                    n = src[s] + 0x12
                    s += 1
                else:
                    n += 2
                for _ in range(n):
                    dst.append(dst[-dist])
    return bytes(dst)


class Rel:
    def __init__(self, data):
        self.data = bytearray(data)
        (self.id, _, _, self.nsec, self.sec_off, _, _, self.version, self.bss_size,
         self.rel_off, self.imp_off, self.imp_size) = struct.unpack_from(">IIIIIIIIIIII", data, 0)
        self.prolog_sec, self.epilog_sec, self.unres_sec, self.bss_sec = data[0x30:0x34]
        self.prolog, self.epilog, self.unres = struct.unpack_from(">III", data, 0x34)
        self.align, self.bss_align, self.fix_size = (struct.unpack_from(">III", data, 0x40) if self.version >= 2 else (32, 32, 0))
        self.sections = []
        for i in range(self.nsec):
            off, size = struct.unpack_from(">II", data, self.sec_off + 8 * i)
            self.sections.append((off & ~1, size, bool(off & 1)))

    def layout(self, base):
        """Section addresses as the recomp places a REL (bss right after the last real section)."""
        addrs = {}
        end = 0
        for i, (off, size, ex) in enumerate(self.sections):
            if off:
                addrs[i] = base + off
                end = max(end, off + size)
        for i, (off, size, ex) in enumerate(self.sections):
            if off == 0 and size:
                a = self.bss_align or 32
                addrs[i] = base + ((end + a - 1) // a * a)
        return addrs

    def imports(self):
        out = []
        for i in range(self.imp_size // 8):
            mid, off = struct.unpack_from(">II", self.data, self.imp_off + 8 * i)
            out.append((mid, off))
        return out

    def relocs(self, off):
        i = off
        while True:
            o, t, s, add = struct.unpack_from(">HBBI", self.data, i)
            yield o, t, s, add
            if t == 203:
                return
            i += 8


def main():
    arc_path, rels_dir, ports_dir = sys.argv[1:4]
    base = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x80B8E3C0
    files = u8_files(open(arc_path, "rb").read())
    rel_name = next(k for k in files if k.endswith("rels/d_project.rel.szs"))
    imp_name = next(k for k in files if k.endswith("rels/d_project_E2.imp.szs"))
    smap_name = next((k for k in files if k.endswith("rels/d_project.SMAP")), None)
    rel_raw = yaz0(files[rel_name])
    imp_raw = yaz0(files[imp_name])
    print(f"arc: {len(files)} files; {rel_name} -> {len(rel_raw)} bytes; {imp_name} -> {len(imp_raw)} bytes")

    mod = Rel(rel_raw)
    print(f"module id {mod.id}, {mod.nsec} sections, version {mod.version}, impOffset 0x{mod.imp_off:X} "
          f"impSize 0x{mod.imp_size:X} relOffset 0x{mod.rel_off:X} bss 0x{mod.bss_size:X} fix 0x{mod.fix_size:X}")
    # The REL's own import table points the game modules' relocation lists past the end of the file:
    # the region's .imp holds those lists and is placed right after the REL (Loader.cpp).
    mod = Rel(bytes(mod.data) + imp_raw)
    print(f"imports: {[(m, hex(o)) for m, o in mod.imports()]}")

    # The game's modules as the recomp has them.
    game = {0: None}
    for yml in os.listdir(ports_dir):
        m = re.match(r"nsmbw_us-(.+)\.yml$", yml)
        if not m:
            continue
        txt = open(os.path.join(ports_dir, yml)).read()
        la = int(re.search(r"load_address:\s*(0x[0-9A-Fa-f]+)", txt).group(1), 16)
        r = Rel(open(os.path.join(rels_dir, m.group(1) + ".rel"), "rb").read())
        game[r.id] = (m.group(1), r.layout(la))
        print(f"game REL {m.group(1)}: id {r.id}, load 0x{la:X}")

    own = mod.layout(base)
    print("module sections:", {i: hex(a) for i, a in own.items()}, "sizes", [hex(s[1]) for s in mod.sections])

    def target(mid, sec, add):
        if mid == 0:
            return add
        if mid == mod.id:
            return own[sec] + add
        return game[mid][1][sec] + add

    img = bytearray(mod.data)
    reloc_target = {}   # module address of the patched word -> (module id it points into, value)
    counts = {}
    for mid, off in mod.imports():
        cur_sec, cur = None, 0
        for o, t, s, add in mod.relocs(off):
            cur += o
            if t == 202:
                cur_sec, cur = s, 0
                continue
            if t in (201, 203):
                continue
            where_file = mod.sections[cur_sec][0] + cur
            P = own[cur_sec] + cur
            S = target(mid, s, add)
            counts[mid] = counts.get(mid, 0) + 1
            if t == 1:
                struct.pack_into(">I", img, where_file, S & 0xFFFFFFFF)
                reloc_target[P] = (mid, S)
            elif t == 4:
                struct.pack_into(">H", img, where_file, S & 0xFFFF)
            elif t == 5:
                struct.pack_into(">H", img, where_file, (S >> 16) & 0xFFFF)
            elif t == 6:
                struct.pack_into(">H", img, where_file, ((S + 0x8000) >> 16) & 0xFFFF)
            elif t == 10:
                v = struct.unpack_from(">I", img, where_file)[0]
                struct.pack_into(">I", img, where_file, (v & 0xFC000003) | ((S - P) & 0x03FFFFFC))
            elif t == 11:
                v = struct.unpack_from(">I", img, where_file)[0]
                struct.pack_into(">I", img, where_file, (v & 0xFFFF0003) | ((S - P) & 0xFFFC))
            elif t == 3:
                struct.pack_into(">H", img, where_file, S & 0xFFFF)
            else:
                counts.setdefault("unknown types", set()).add(t)
    print("relocations by target module:", {k: v for k, v in counts.items()})

    def word(addr):
        for i, a in own.items():
            off, size, ex = mod.sections[i]
            if off and a <= addr < a + size:
                return struct.unpack_from(">I", img, off + addr - a)[0]
        return None

    text_secs = [i for i, s in enumerate(mod.sections) if s[2] and s[0]]
    data_secs = [i for i, s in enumerate(mod.sections) if not s[2] and s[0] and s[1]]
    def in_mod_text(v):
        return any(own[i] <= v < own[i] + mod.sections[i][1] for i in text_secs)
    def game_ptr(P):
        t = reloc_target.get(P)
        return t is not None and t[0] != mod.id

    data_sec = data_secs[0]
    d0, dsize = own[data_sec], mod.sections[data_sec][1]
    print(f".data section {data_sec} at 0x{d0:X} size 0x{dsize:X}; text sections {text_secs}")

    # replace_array: pairs (game function, mod function) from the start of .data.
    p = d0
    replace = []
    while game_ptr(p) and in_mod_text(word(p + 4)):
        replace.append((word(p), word(p + 4)))
        p += 8
    # patch_references_array: dest, count, count x {u8 type, pad, s16 addend, u32 address}.
    refs = []
    while True:
        cnt = word(p + 4)
        if cnt is None or not (1 <= cnt <= 256):
            break
        ok = True
        items = []
        for k in range(cnt):
            e = p + 8 + 8 * k
            t = (word(e) >> 24) & 0xFF
            if t not in (4, 5, 6) or not (game_ptr(e + 4) or word(e + 4) == 0):
                ok = False
                break
            items.append((t, word(e + 4)))
        if not ok:
            break
        refs.append((word(p), items))
        p += 8 + 8 * cnt
    # extern_array: (game function, mod thunk in "extern": `nop; b ...`).
    externs = []
    while game_ptr(p) and in_mod_text(word(p + 4)):
        externs.append((word(p), word(p + 4)))
        p += 8
    print(f"replace_array: {len(replace)} entries; patch_references_array: {len(refs)} entries "
          f"({sum(len(i) for _, i in refs)} references); extern_array: {len(externs)} entries; "
          f"tables end at 0x{p:X} (+0x{p - d0:X})")
    for g, m in replace[:5]:
        print(f"  replace 0x{g:08X} -> 0x{m:08X}")
    for g, m in externs[:3]:
        print(f"  extern  0x{g:08X} thunk 0x{m:08X}: {word(m):08X} {word(m + 4):08X}")

    # FOUR_PATCH_LIST: runs of {game address (relocated), u8 size 1..4, s8 offset, pad}.
    best = (0, 0)
    a = d0
    while a < d0 + dsize - 8:
        n = 0
        q = a
        while q < d0 + dsize - 8 and game_ptr(q) and 1 <= (word(q + 4) >> 24) <= 4:
            n += 1
            q += 8
        if n > best[1]:
            best = (a, n)
        a = q + 4 if n else a + 4
    print(f"FOUR_PATCH_LIST candidate: {best[1]} entries at 0x{best[0]:X}")

    games = [g for g, _ in replace]
    in_dol = sum(1 for g in games if g < 0x80768000)
    print(f"replaced functions: {in_dol} in main.dol, {len(games) - in_dol} in the RELs; "
          f"null addresses {sum(1 for g in games if g == 0)}")
    for addr in (0x8015FC10, 0x801AD4E0):
        if addr in games:
            print(f"  overlaps a recomp override: 0x{addr:08X}")
    if smap_name:
        smap = files[smap_name]
        print(f"SMAP: {len(smap)} bytes, first line: {smap[:120]!r}")


if __name__ == "__main__":
    main()
