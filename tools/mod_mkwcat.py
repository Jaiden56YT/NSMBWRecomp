"""mkwcat's nsmbw-project (github.com/mkwcat/nsmbw-project, MPL-2.0) for tools/mod_build.py.

The pack's Riivolution XML points a boot-scene state at a loader blob (0x800047E0, US). On a
console that loader reads rels/d_project.rel.szs and the region's rels/d_project_E2.imp.szs from
/mkwcat.arc, OSLinks the module on the game heap and calls its _prolog(arcEntryNum, &arcHandle).
_prolog then rewrites the game: a `b` to the mod at the start of every replaced game function
(replace_array), 16-bit immediates pointed at the mod's enlarged arrays (patch_references_array),
player-count immediates (Four::Apply, FOUR_PATCH_LIST) and the first instruction of each
call-original thunk (extern_array); then preinit, the constructors and main().

The recomp runs only code translated ahead of time, so this does the console's work at build time
(see docs/mods.md 6c):
  - links the module at a fixed address under the MEM1 arena top, against the recomp's layout;
  - recovers the four tables from the linked module's .data (they sit at its start in that order;
    entries are told apart by the relocations that fill them) and returns the code writes _prolog
    would make, in its order, for mod_build to bake into the main.dol / REL copies;
  - copies each thunk's game instruction into the module, as _prolog does;
  - embeds the arc header and an ARCHandle for the native loader (mod_build's generated source),
    which calls the game's DVDConvertPathToEntrynum / ARCInitHandle and the module's _prolog.
_prolog still runs in full at boot: its writes to game code change nothing (that code is already
translated, with the same changes), and its writes to game data are the ones that must happen then.
"""

import struct

LOADER = 0x800047E0                        # the XML's loader blob (US), replaced natively
OPTION_BYTES = (0x80006780, 0x800067A0)    # the XML's option flags: data the module reads
ARC_INIT_HANDLE = 0x8019F660               # US
DVD_CONVERT_PATH = 0x801CA680              # US
SMAP_BASE = 0x80B8E3C0                     # the address the pack's SMAP lists the module text at
ARC_NAME = "mkwcat.arc"


def u8_files(data):
    if data[:4] != b"\x55\xAA\x38\x2D":
        raise ValueError("not a U8 archive")
    root = struct.unpack_from(">I", data, 4)[0]
    n = struct.unpack_from(">I", data, root + 8)[0]
    strings = root + n * 12
    out, stack = {}, []
    for i in range(1, n):
        while stack and stack[-1][0] <= i:
            stack.pop()
        typ = data[root + i * 12]
        name_off = struct.unpack_from(">I", data, root + i * 12)[0] & 0xFFFFFF
        a, b = struct.unpack_from(">II", data, root + i * 12 + 4)
        end = data.index(b"\0", strings + name_off)
        name = data[strings + name_off:end].decode("latin1")
        if typ == 1:
            stack.append((b, name))
        else:
            out["/".join([s[1] for s in stack] + [name])] = data[a:a + b]
    return out


def yaz0(src):
    if src[:4] != b"Yaz0":
        return src
    size = struct.unpack_from(">I", src, 4)[0]
    dst, s = bytearray(), 16
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
        self.data = data
        (self.id, _, _, self.nsec, self.sec_off, _, _, self.version, self.bss_size,
         self.rel_off, self.imp_off, self.imp_size) = struct.unpack_from(">IIIIIIIIIIII", data, 0)
        self.prolog_sec = data[0x30]
        self.prolog = struct.unpack_from(">I", data, 0x34)[0]
        self.bss_align, self.fix_size = (struct.unpack_from(">I", data, 0x44)[0], struct.unpack_from(">I", data, 0x48)[0]) \
            if self.version >= 3 else (32, 0)
        self.sections = []
        for i in range(self.nsec):
            off, size = struct.unpack_from(">II", data, self.sec_off + 8 * i)
            self.sections.append((off & ~1, size, bool(off & 1)))

    def game_layout(self, base):
        """Section addresses as the recomp places a game REL. BSS goes where the console's
        DynamicModuleControl::do_link puts it, at ROUND_UP(fixSize, 0x20), not right after the last
        section (translator RelFile.cs: for d_basesNP the two differ by 0x8F8, and the mod's state
        IDs in that BSS then pointed at other objects). Section end only for headers without fixSize."""
        addrs, end = {}, 0
        for i, (off, size, _) in enumerate(self.sections):
            if off:
                addrs[i] = base + off
                end = max(end, off + size)
        if self.fix_size and self.fix_size >= end:
            bss = (self.fix_size + 0x1F) // 0x20 * 0x20
        else:
            a = self.bss_align or 32
            bss = (end + a - 1) // a * a
        for i, (off, size, _) in enumerate(self.sections):
            if off == 0 and size:
                addrs[i] = base + bss
        return addrs

    def imports(self):
        return [struct.unpack_from(">II", self.data, self.imp_off + 8 * i) for i in range(self.imp_size // 8)]

    def relocs(self, off):
        while True:
            o, t, s, add = struct.unpack_from(">HBBI", self.data, off)
            yield o, t, s, add
            if t == 203:
                return
            off += 8


class MkwcatModule:
    """What mod_build needs (the KamekModule shape) plus the native loader's addresses."""

    def __init__(self, **kw):
        self.__dict__.update(kw)
        self.ctors = []          # _prolog runs them itself
        self.loader = LOADER
        self.kind = "mkwcat"


def find_pack(mod_dir):
    """The pack's .arc: a U8 archive in the mod folder holding rels/d_project.rel.szs."""
    for path in sorted(mod_dir.rglob("*.arc")):
        data = path.read_bytes()
        try:
            files = u8_files(data)
        except (ValueError, struct.error, IndexError):
            continue
        if any(k.endswith("rels/d_project.rel.szs") for k in files):
            return path, data, files
    return None


def is_loader_patch(patch):
    return patch["offset"] == LOADER and len(patch["value"]) > 64


def is_option_byte(patch):
    return OPTION_BYTES[0] <= patch["offset"] < OPTION_BYTES[1]


def link(mod_dir, image, game_rels, module_top, region="E2"):
    """Links the pack's module under module_top. game_rels: {name: (load address, rel bytes)}.
    image: mod_build's GameImage (reads the vanilla main.dol / RELs). Returns MkwcatModule."""
    found = find_pack(mod_dir)
    if found is None:
        raise SystemExit("  mkwcat: no .arc with rels/d_project.rel.szs in the mod folder")
    arc_path, arc_data, files = found
    rel_raw = yaz0(files[next(k for k in files if k.endswith("rels/d_project.rel.szs"))])
    imp_raw = yaz0(files[next(k for k in files if k.endswith(f"rels/d_project_{region}.imp.szs"))])
    smap_key = next((k for k in files if k.endswith("rels/d_project.SMAP")), None)
    # The module's import table points the game modules' relocation lists past the end of the
    # REL: the region's .imp is placed right after it (loader/Loader.cpp).
    mod = Rel(rel_raw + imp_raw)

    game = {}
    for name, (load, data) in game_rels.items():
        r = Rel(data)
        game[r.id] = r.game_layout(load)

    fix = (mod.fix_size + 31) & ~31
    arc_header_size = (struct.unpack_from(">I", arc_data, 12)[0] + 31) & ~31
    # Image: [REL up to fixSize][bss][ARC header][ARCHandle 0x40][name]
    extra = arc_header_size + 0x40 + 32
    total = fix + mod.bss_size + extra
    total = (total + 31) & ~31
    base = (module_top - total) & ~0x1F
    own = {}
    for i, (off, size, _) in enumerate(mod.sections):
        if off:
            own[i] = base + off
        elif size:
            own[i] = base + fix          # OSLinkFixed: bss right after the fixed part
    code = bytearray(rel_raw[:fix]) + bytearray(total - fix)

    def target(mid, sec, add):
        if mid == 0:
            return add
        if mid == mod.id:
            return own[sec] + add
        return game[mid][sec] + add

    pointer_into = {}   # module address of an ADDR32 word -> module id it points into
    text_targets = set()
    text_sec = next(i for i, s in enumerate(mod.sections) if s[2] and s[0])
    text_lo, text_hi = own[text_sec], own[text_sec] + mod.sections[text_sec][1]
    for mid, off in mod.imports():
        sec, cur = None, 0
        for o, t, s, add in mod.relocs(off):
            cur += o
            if t == 202:
                sec, cur = s, 0
                continue
            if t in (201, 203):
                continue
            P = own[sec] + cur
            at = P - base
            S = target(mid, s, add)
            if mid == mod.id and text_lo <= S < text_hi and t in (1, 10):
                text_targets.add(S)
            if t == 1:
                struct.pack_into(">I", code, at, S & 0xFFFFFFFF)
                pointer_into[P] = mid
            elif t in (3, 4):
                struct.pack_into(">H", code, at, S & 0xFFFF)
            elif t == 5:
                struct.pack_into(">H", code, at, (S >> 16) & 0xFFFF)
            elif t == 6:
                struct.pack_into(">H", code, at, ((S + 0x8000) >> 16) & 0xFFFF)
            elif t == 10:
                v = struct.unpack_from(">I", code, at)[0]
                struct.pack_into(">I", code, at, (v & 0xFC000003) | ((S - P) & 0x03FFFFFC))
            elif t == 11:
                v = struct.unpack_from(">I", code, at)[0]
                struct.pack_into(">I", code, at, (v & 0xFFFF0003) | ((S - P) & 0xFFFC))
            else:
                raise SystemExit(f"  mkwcat: unsupported relocation type {t}")

    def word(addr):
        return struct.unpack_from(">I", code, addr - base)[0]

    def game_ptr(addr):
        mid = pointer_into.get(addr)
        return mid is not None and mid != mod.id

    def in_text(v):
        return text_lo <= v < text_hi

    data_sec = next(i for i, s in enumerate(mod.sections) if not s[2] and s[0] and s[1])
    d0, dsize = own[data_sec], mod.sections[data_sec][1]
    p, replace, refs, externs = d0, [], [], []
    while game_ptr(p) and in_text(word(p + 4)):
        replace.append((word(p), word(p + 4)))
        p += 8
    while True:
        cnt = word(p + 4)
        if not 1 <= cnt <= 256:
            break
        items = []
        for k in range(cnt):
            e = p + 8 + 8 * k
            kind, addend = word(e) >> 24, struct.unpack(">h", struct.pack(">H", word(e) & 0xFFFF))[0]
            if kind not in (4, 5, 6) or not (game_ptr(e + 4) or word(e + 4) == 0):
                items = None
                break
            items.append((kind, addend, word(e + 4)))
        if items is None:
            break
        refs.append((word(p), items))
        p += 8 + 8 * cnt
    while game_ptr(p) and in_text(word(p + 4)):
        externs.append((word(p), word(p + 4)))
        p += 8
    four_at, four_n = 0, 0
    a = d0
    while a < d0 + dsize - 8:
        q, n = a, 0
        while q < d0 + dsize - 8 and game_ptr(q) and 1 <= (word(q + 4) >> 24) <= 4:
            n, q = n + 1, q + 8
        if n > four_n:
            four_at, four_n = a, n
        a = q + 4 if n else a + 4
    four = [(word(four_at + 8 * i), word(four_at + 8 * i + 4) >> 24, (word(four_at + 8 * i + 4) >> 16) & 0xFF)
            for i in range(four_n)]

    # The game writes _prolog makes, in its order. game_bytes tracks values already written, so a
    # later write reads them (as on the console).
    patches, written = [], {}

    def read_game(addr, n):
        out = bytearray()
        for k in range(n):
            if addr + k in written:
                out.append(written[addr + k])
            else:
                b = image.read(addr + k, 1)
                out += b if b is not None else b"\0"
        return bytes(out)

    def write_game(addr, value):
        patches.append((addr, bytes(value)))
        for k, b in enumerate(value):
            written[addr + k] = b

    import os
    experiment_no_refs = os.environ.get("MKWCAT_EXPERIMENT_NO_REFS") == "1"
    if experiment_no_refs:
        print("  mkwcat: EXPERIMENT: reference and player-count patches not baked")
    for dest, items in ([] if experiment_no_refs else refs):
        for kind, addend, addr in items:
            if addr == 0:
                continue
            off = (dest + addend) & 0xFFFFFFFF
            half = off & 0xFFFF if kind == 4 else (off >> 16) if kind == 5 else ((off + 0x8000) >> 16) & 0xFFFF
            write_game(addr, struct.pack(">H", half))
    # Four.cpp: FourPatch{address, u8 size, s8 offset}; offsets <= NEGATIVE (-64) mean a negative
    # value: old -(4 + offset + 64), new -(PLAYER_COUNT + offset + 64).
    PLAYERS, NEGATIVE = 8, -64
    for addr, size, raw in ([] if experiment_no_refs else four):
        off = raw - 256 if raw >= 128 else raw
        negative = off <= NEGATIVE
        if negative:
            off -= NEGATIVE
        new = PLAYERS + off
        if negative:
            new = -new
        write_game(addr, new.to_bytes(4, "big", signed=True)[4 - size:])
    for addr, thunk in externs:
        code[thunk - base:thunk - base + 4] = read_game(addr, 4)
    # Replaced functions: gated, not branched directly (see GATES below).

    # The native loader's inputs: the arc header (as the loader reads it), a handle, the name.
    arc_header_at = base + fix + mod.bss_size
    arc_header_at = (arc_header_at + 31) & ~31
    code[arc_header_at - base:arc_header_at - base + arc_header_size] = arc_data[:arc_header_size]
    handle_at = arc_header_at + arc_header_size
    name_at = handle_at + 0x40
    code[name_at - base:name_at - base + len(ARC_NAME) + 1] = ARC_NAME.encode() + b"\0"

    # GATES. On a console the `b` to the mod is written into each replaced game function by _prolog,
    # i.e. only once the module is loaded and initialised; until then (the first boot-scene frames)
    # the game runs its own functions. Baked straight in, the mod's replacements ran from the first
    # frame with the module's data not set up (mPad's replacement called a null pointer). So each
    # replaced function starts with a `b` to a gate that checks a flag the native loader sets right
    # before _prolog (where the console's branches would take effect):
    #     lis r12, flag@ha; lwz r12, flag@l(r12); cmpwi r12, 0; beq 1f; b <mod function>
    #  1: <the function's own first instruction, moved>; b <function + 4>
    # r12 and cr0 are free at a function's entry (volatile in the PowerPC EABI).
    flag_at = name_at + 32 - 4
    gates = bytearray()
    gate_of = {}
    gate_count = len(replace)
    gate_size = (gate_count * 28 + 31) & ~31
    gates_base = (base - gate_size) & ~0x1F
    for i, (addr, dest) in enumerate(replace):
        g = gates_base + i * 28
        gate_of[addr] = g
        first = struct.unpack(">I", read_game(addr, 4))[0]
        if first >> 26 == 18 and not first & 2:   # b / bl (relative): retarget from the moved spot
            li = first & 0x03FFFFFC
            target = addr + (li - 0x04000000 if li & 0x02000000 else li)
            first = (first & 0xFC000003) | ((target - (g + 20)) & 0x03FFFFFC)
        elif first >> 26 == 16 and not first & 2:  # bc (relative)
            bd = first & 0xFFFC
            target = addr + (bd - 0x10000 if bd & 0x8000 else bd)
            first = (first & 0xFFFF0003) | ((target - (g + 20)) & 0xFFFC)
        words = [
            0x3D800000 | (((flag_at + 0x8000) >> 16) & 0xFFFF),   # lis r12, flag@ha
            0x818C0000 | (flag_at & 0xFFFF),                      # lwz r12, flag@l(r12)
            0x2C0C0000,                                           # cmpwi r12, 0
            0x41820008,                                           # beq +8
            0x48000000 | ((dest - (g + 16)) & 0x03FFFFFC),        # b mod function
            first,                                                # the game function's first insn
            0x48000000 | ((addr + 4 - (g + 24)) & 0x03FFFFFC),    # b function + 4
        ]
        gates += b"".join(struct.pack(">I", w) for w in words)
        write_game(addr, struct.pack(">I", 0x48000000 | ((g - addr) & 0x03FFFFFC)))
    gates += bytes(gate_size - len(gates))
    code = bytearray(gates) + bytes(base - gates_base - gate_size) + code

    seeds = {own[mod.prolog_sec] + mod.prolog} | set(gate_of.values())
    seeds |= {d for _, d in replace} | {t for _, t in externs} | text_targets
    smap_names = {}
    if smap_key:
        for line in files[smap_key].decode("latin1").splitlines():
            parts = line.split(None, 1)
            if len(parts) == 2 and parts[0].startswith("0x"):
                addr = int(parts[0], 16)
                if addr >= SMAP_BASE:
                    rebased = addr - SMAP_BASE + own[text_sec]
                    if in_text(rebased):
                        seeds.add(rebased)
                        smap_names.setdefault(rebased, parts[1].strip())
    # Call-original thunks and gates continue at the game function + 4: an entry point of its own.
    game_seeds = sorted({addr + 4 for addr, _ in externs} | {addr + 4 for addr, _ in replace})
    flush = next((a for a, n in smap_names.items() if n.startswith("__flush_entire_cache")), None)
    # The mod's own PADRead (its copy of the SDK PAD library): replaced natively so players 5-8
    # (GameCube ports) can be fed (native_us/nsmbw_netplay_lockstep.cpp, NsmbwModPadRead).
    pad_read = next((a for a, n in smap_names.items() if n == "PADRead"), None)

    print(f"  mkwcat: {arc_path.name}: module {len(rel_raw)} + imports {len(imp_raw)} bytes linked at "
          f"{base:08X} (text {text_lo:08X}-{text_hi:08X}, bss at {own.get(3, 0):08X})")
    print(f"  mkwcat: {len(replace)} replaced functions, {len(refs)} reference patches "
          f"({sum(len(i) for _, i in refs)} immediates), {len(externs)} call-original thunks, "
          f"{len(four)} player-count patches -> {len(patches)} game writes; {len(seeds)} module entry points; "
          f"{gate_count} gates at {gates_base:08X} (flag {flag_at:08X})")
    return MkwcatModule(
        name="d_project", base=gates_base, code=bytes(code), text_size=text_hi - gates_base, flag=flag_at,
        game_patches=patches, seeds=sorted(seeds), game_seeds=game_seeds,
        prolog=own[mod.prolog_sec] + mod.prolog, arc_header=arc_header_at, handle=handle_at, arc_name=name_at,
        flush=flush, pad_read=pad_read, replaced=[a for a, _ in replace])
