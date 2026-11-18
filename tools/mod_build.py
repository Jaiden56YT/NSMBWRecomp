#!/usr/bin/env python3
"""Build a mod-specific NSMBW translation: Riivolution code patches baked in.

The runtime applies a mod's file replacements and *data* <memory> patches by itself
(runtime/src/hle/storage/riivolution*.cpp). Patches to *code* cannot work that way: the
game's PowerPC code was translated to C++ ahead of time, so writing new instructions into
guest RAM changes nothing. This tool does what Riivolution would do to the executable, but
before translation:

  1. read mods/<name>/riivolution/*.xml with the same option rules as the runtime
     (mod.toml [options], riivolution/config/SMNE.xml, else every option's first choice);
  2. apply each active <memory> patch that lands in main.dol's or a REL's .text to a copy
     of code/main.dol / code/rels/*.rel (only where `original` matches, as Riivolution does);
  3. re-translate everything from the patched copies into mod_builds/<name>/ (~2 min) and
     emit a shard graph + data initializer there;
  4. optionally configure and build a separate CMake build dir that uses them.

Data patches are left to the runtime (they are applied at boot either way). Patches outside
every known section (e.g. a loader blob at 0x80001800) are reported: running new code needs
it translated as an extra module, which this prototype does not do yet (see docs/mods.md).

The outputs contain translated game code and game data: never share or commit them
(mod_builds/ is gitignored).

Usage (from the repo root):
  python3 -I tools/mod_build.py <mod dir> [--name NAME] [--dry-run] [--configure] [--build]
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tomllib
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PORT = REPO / "ports" / "wiicompiled"
UPSTREAM = REPO / "third_party" / "NSMBWiicompiled"
# MOD_BUILD_TRANSLATOR overrides the translator build to use (e.g. one from another checkout).
TRANSLATOR = Path(os.environ.get("MOD_BUILD_TRANSLATOR") or
                  UPSTREAM / "translator" / "src" / "Translator.Cli" / "bin" / "Release" / "net8.0" / "Translator.Cli.dll")

GAME_ID = "SMNE01"  # USA; the only version this port translates (Rev 2)

# name -> (load address, _prolog entry) for the four RELs, as in modules.txt / translate_all.sh
RELS = {
    "d_profileNP": (0x80768500, 0x807685E0),
    "d_basesNP": (0x8076D6C0, 0x8076D7B0),
    "d_enemiesNP": (0x809A2CE0, 0x809A2DD0),
    "d_en_bossNP": (0x80B1C960, 0x80B1CA50),
}
DOL_ENTRY = 0x80004050


# ---------------------------------------------------------------- binaries

class Section:
    def __init__(self, module, path, file_offset, address, size, executable):
        self.module, self.path = module, path
        self.file_offset, self.address, self.size = file_offset, address, size
        self.executable = executable

    def contains(self, address, length):
        return self.address <= address and address + length <= self.address + self.size

    def __repr__(self):
        kind = "text" if self.executable else "data"
        return f"{self.module} {kind} {self.address:08X}-{self.address + self.size:08X}"


def dol_sections(path):
    d = path.read_bytes()
    offs = struct.unpack(">18I", d[0:72])
    addrs = struct.unpack(">18I", d[72:144])
    sizes = struct.unpack(">18I", d[144:216])
    return [Section("main.dol", path, offs[i], addrs[i], sizes[i], i < 7)
            for i in range(18) if sizes[i]]


def rel_sections(path, name, base):
    d = path.read_bytes()
    count, table = struct.unpack(">II", d[0x0C:0x14])
    out = []
    for i in range(count):
        off, size = struct.unpack(">II", d[table + i * 8:table + i * 8 + 8])
        if size and off & ~3:  # bss has offset 0 (not in the file)
            out.append(Section(name, path, off & ~3, base + (off & ~3), size, bool(off & 1)))
    return out


# ---------------------------------------------------------------- riivolution

def hex_bytes(text):
    text = re.sub(r"0x|0X|\s", "", text or "")
    if len(text) % 2 or not re.fullmatch(r"[0-9a-fA-F]*", text):
        raise ValueError(f"bad hex value {text!r}")
    return bytes.fromhex(text)


def resolve_external(sd_root, patch_root, external):
    """Dolphin's rule: '/x' is relative to the SD root, 'x' to the patch root."""
    if "\\" in external or ".." in Path(external).parts:
        return None
    base = sd_root if external.startswith("/") else patch_root
    return base / external.lstrip("/")


def load_mod_settings(mod_dir):
    path = mod_dir / "mod.toml"
    if not path.is_file():
        return {}, False
    doc = tomllib.loads(path.read_text())
    options = doc.get("options")
    return (options or {}), options is not None


def active_patches(mod_dir):
    """Yields (xml path, patch element, params) for every patch the runtime would activate."""
    settings, has_options = load_mod_settings(mod_dir)
    config = {}
    config_xml = mod_dir / "riivolution" / "config" / (GAME_ID[:4] + ".xml")
    if config_xml.is_file():
        for opt in ET.parse(config_xml).getroot().iter("option"):
            config[opt.get("id")] = int(opt.get("default", "0"), 0)

    for xml in sorted((mod_dir / "riivolution").glob("*.xml")):
        root = ET.parse(xml).getroot()
        if root.tag != "wiidisc" or root.get("version") != "1":
            continue
        ident = root.find("id")
        if ident is not None:
            if ident.get("game") and not GAME_ID.startswith(ident.get("game")):
                continue
            regions = [r.get("type") for r in ident.findall("region")]
            if regions and GAME_ID[3] not in regions:
                continue
        patches = {p.get("id"): p for p in root.findall("patch")}
        default_root = root.get("root", "")

        options = []  # (section name, option element, selected)
        for section in root.iter("section"):
            for option in section.findall("option"):
                selected = int(option.get("default", "0"), 0)
                key = option.get("id") or (section.get("name", "") + option.get("name", ""))
                selected = config.get(key, selected)
                for name in (section.get("name", "") + "/" + option.get("name", ""), option.get("id"), option.get("name")):
                    if name and name in settings:
                        value = settings[name]
                        selected = (1 if value else 0) if isinstance(value, bool) else int(value)
                        break
                options.append([section, option, selected])
        if not config and not has_options and all(sel == 0 for _, _, sel in options):
            for entry in options:
                if entry[1].findall("choice"):
                    entry[2] = 1

        for section, option, selected in options:
            choices = option.findall("choice")
            if selected < 1 or selected > len(choices):
                continue
            option_params = {p.get("name"): p.get("value") for p in option.findall("param")}
            choice = choices[selected - 1]
            choice_params = dict(option_params, **{p.get("name"): p.get("value") for p in choice.findall("param")})
            for ref in choice.findall("patch"):
                patch = patches.get(ref.get("id"))
                if patch is None:
                    continue
                params = dict(choice_params, **{p.get("name"): p.get("value") for p in ref.findall("param")})
                yield xml, patch, params, patch.get("root") or default_root


def substitute(text, params):
    replacements = {"{$__gameid}": GAME_ID[:3], "{$__region}": GAME_ID[3], "{$__maker}": GAME_ID[4:]}
    replacements.update({"{$" + k + "}": v for k, v in params.items()})
    for key, value in replacements.items():
        text = text.replace(key, value)
    return text


def memory_patches(mod_dir):
    for xml, patch, params, root_attr in active_patches(mod_dir):
        xml_dir = xml.parent
        patch_root = xml_dir
        if root_attr:
            resolved = resolve_external(mod_dir, xml_dir, substitute(root_attr, params))
            patch_root = resolved or xml_dir
        for mem in patch.findall("memory"):
            entry = {
                "source": f"{xml.name}: patch '{patch.get('id')}'",
                "offset": int(mem.get("offset", "0"), 0),
                "original": hex_bytes(mem.get("original")) if mem.get("original") else b"",
                "search": mem.get("search") in ("true", "1", "yes"),
                "align": int(mem.get("align", "1"), 0) or 1,
                "ocarina": mem.get("ocarina") in ("true", "1", "yes"),
                "valuefile": None,
            }
            if mem.get("valuefile"):
                path = resolve_external(mod_dir, patch_root, substitute(mem.get("valuefile"), params))
                if not path or not path.is_file():
                    print(f"  ! {entry['source']}: valuefile {mem.get('valuefile')} not found", file=sys.stderr)
                    continue
                entry["value"] = path.read_bytes()
                entry["valuefile"] = str(path)
            else:
                entry["value"] = hex_bytes(mem.get("value"))
            yield entry


def file_mappings(mod_dir):
    """(disc path or None, host path, is_folder) for every active <file>/<folder>, in order."""
    out = []
    for xml, patch, params, root_attr in active_patches(mod_dir):
        patch_root = xml.parent
        if root_attr:
            patch_root = resolve_external(mod_dir, xml.parent, substitute(root_attr, params)) or xml.parent
        for node in patch:
            if node.tag not in ("file", "folder"):
                continue
            host = resolve_external(mod_dir, patch_root, substitute(node.get("external", ""), params))
            if host is not None:
                out.append((substitute(node.get("disc", ""), params) or None, host, node.tag == "folder"))
    return out


def resolve_disc_file(mappings, disc_path):
    """Host file the runtime would serve for a disc path (last mapping wins, as in dvd.cpp)."""
    found = None
    lower = disc_path.lower()
    for disc, host, folder in mappings:
        if not folder and disc and disc.lower() == lower and host.is_file():
            found = host
        elif folder and disc:
            prefix = disc.rstrip("/").lower() + "/"
            if lower.startswith(prefix):
                candidate = host / disc_path[len(prefix):]
                if candidate.is_file():
                    found = candidate
        elif folder and not disc:
            candidate = next(host.rglob(Path(disc_path).name), None)
            if candidate is not None and candidate.is_file():
                found = candidate
    if found is None:
        plain = REPO / "extracted" / "files" / disc_path.lstrip("/")
        if plain.is_file():
            found = plain
    return found


# ---------------------------------------------------------------- game image

class GameImage:
    """Copies of main.dol and the four RELs, addressed by guest address."""

    def __init__(self):
        self.files = {"main.dol": bytearray((REPO / "code" / "main.dol").read_bytes())}
        self.sections = dol_sections(REPO / "code" / "main.dol")
        for name, (base, _) in RELS.items():
            self.files[name] = bytearray((REPO / "code" / "rels" / f"{name}.rel").read_bytes())
            self.sections += rel_sections(REPO / "code" / "rels" / f"{name}.rel", name, base)
        self.rel_relocs = {name: self._rel_reloc_sites(name) for name in RELS}

    def section_for(self, address, length):
        return next((s for s in self.sections if s.contains(address, length)), None)

    def read(self, address, length):
        section = self.section_for(address, length)
        if section is None:
            return None
        start = section.file_offset + address - section.address
        return bytes(self.files[section.module][start:start + length])

    def write(self, address, value):
        section = self.section_for(address, len(value))
        start = section.file_offset + address - section.address
        self.files[section.module][start:start + len(value)] = value
        if section.module in RELS:
            self._drop_rel_relocs(section.module, address, len(value))
        return section

    # A REL's own relocations are applied when the translator (and the console) link it, after
    # the file bytes. A patch on a relocated word would be overwritten, so such relocations are
    # turned into R_RVL_NOP (201), which keeps the offset chain intact.
    def _rel_reloc_sites(self, name):
        d = self.files[name]
        base = RELS[name][0]
        count, table = struct.unpack(">II", d[0x0C:0x14])
        section_offsets = [struct.unpack(">I", d[table + i * 8:table + i * 8 + 4])[0] & ~3 for i in range(count)]
        imp_offset, imp_size = struct.unpack(">II", d[0x28:0x30])
        sites = []  # (guest address, length, file offset of the reloc entry)
        for i in range(imp_size // 8):
            _, rel_offset = struct.unpack(">II", d[imp_offset + i * 8:imp_offset + i * 8 + 8])
            pos, section_base, cursor = 0, 0, rel_offset
            while True:
                offset, rtype, section = struct.unpack(">HBB", d[cursor:cursor + 4])
                if rtype == 203:  # R_RVL_STOP
                    break
                if rtype == 202:  # R_RVL_SECT
                    section_base, pos = section_offsets[section], 0
                else:
                    pos += offset
                    if rtype not in (0, 201):
                        length = 2 if rtype in (4, 5, 6) else 4
                        sites.append((base + section_base + pos, length, cursor))
                cursor += 8
        return sites

    def _drop_rel_relocs(self, name, address, length):
        d = self.files[name]
        for site, size, entry in self.rel_relocs[name]:
            if site < address + length and address < site + size and d[entry + 2] != 201:
                d[entry + 2] = 201
                print(f"    {name}: relocation at {site:08X} dropped (patched)")

    def write_files(self, out_code, extra_text=None):
        out_code.mkdir(parents=True, exist_ok=True)
        (out_code / "rels").mkdir(exist_ok=True)
        dol = self.files["main.dol"]
        if extra_text is not None:
            # The module's code as a text section, its data (if any) as a data section.
            address, code, text_size = extra_text
            text_size = (text_size + 31) & ~31
            dol = add_dol_section(dol, address, code[:text_size], True)
            if len(code) > text_size:
                dol = add_dol_section(dol, address + text_size, code[text_size:], False)
        (out_code / "main.dol").write_bytes(dol)
        for name in RELS:
            (out_code / "rels" / f"{name}.rel").write_bytes(self.files[name])


def add_dol_section(dol, address, data, text):
    """Copy of a DOL with one more text (slots 0-6) or data (7-17) section."""
    dol = bytearray(dol)
    sizes = struct.unpack(">18I", dol[144:216])
    slot = next(i for i in (range(7) if text else range(7, 18)) if sizes[i] == 0)
    offset = (len(dol) + 31) & ~31
    padded = bytes(data) + b"\0" * ((-len(data)) % 32)
    dol += b"\0" * (offset - len(dol)) + padded
    struct.pack_into(">I", dol, slot * 4, offset)
    struct.pack_into(">I", dol, 72 + slot * 4, address)
    struct.pack_into(">I", dol, 144 + slot * 4, len(padded))
    return dol


# ---------------------------------------------------------------- Kamek (Newer loader format)

NEWER_LOADER = 0x80001800
# Top of the linked Kamek code. The MEM1 arena ends at 0x817F0040 (nsmbw_product.cpp), and the
# runtime lowers the arena top to the module's base at boot. The 64 KiB just below 0x817F0040 stay
# out of the module: the runtime runs __start's first instructions and every guest constructor
# with r1 = 0x817F0040, so their stack frames land there after the module is copied in (seen as
# the module's data being overwritten with a stack frame: back chain 0x817F0030, LR 0x80160A40).
MODULE_TOP = 0x817E0000


class KamekModule:
    def __init__(self, name, base, code, text_size, game_patches, seeds):
        self.name, self.base, self.code, self.text_size = name, base, code, text_size
        self.game_patches = game_patches  # [(address, bytes)] in the loader's order
        self.seeds = seeds                # function entry points inside the module


def parse_kamek_system(data):
    """Kamek System.bin: addr|0x80000000 + word, or count + addr + count words; FFFFFFFF ends."""
    patches, i = [], 0
    while i + 4 <= len(data):
        word = struct.unpack(">I", data[i:i + 4])[0]
        i += 4
        if word == 0xFFFFFFFF:
            break
        if word & 0x80000000:
            patches.append((word, data[i:i + 4]))
            i += 4
        else:
            address = struct.unpack(">I", data[i:i + 4])[0]
            patches.append((address, data[i + 4:i + 4 + word * 4]))
            i += 4 + word * 4
    return patches


def link_newer(mod_dir, image, runtime_mem):
    """Links Newer's DLCode at a fixed MEM1 address, the way NewerSMBW's loader does at runtime.

    Returns None when the mod has no Newer loader. runtime_mem maps addresses outside the
    DOL/REL sections to bytes already patched (low memory), for REL24 read-modify-write."""
    mappings = file_mappings(mod_dir)
    region, version = "US", "2"  # SMNE01 Rev 2, what the loader detects on this disc
    files = {}
    for kind in ("System", "DLCode", "DLRelocs"):
        disc = f"/NewerRes/{kind}{region}_{version}.bin"
        host = resolve_disc_file(mappings, disc)
        if host is None:
            raise SystemExit(f"  Newer loader patch found but {disc} is not in the mod")
        files[kind] = host.read_bytes()
        print(f"  kamek: {disc} <- {host} ({len(files[kind])} bytes)")

    code = bytearray(files["DLCode"])
    code += b"\0" * ((-len(code)) % 32)
    base = (MODULE_TOP - len(code)) & ~0x1F
    game = {}  # address -> bytearray(4) words/halfwords written by System.bin + relocations
    patches = []

    def game_read(address, length):
        if address in game and len(game[address]) == length:
            return bytes(game[address])
        data = image.read(address, length)
        if data is None:
            data = runtime_mem.get(address, b"\0" * length)[:length]
        return data

    for address, data in parse_kamek_system(files["System"]):
        patches.append((address, bytes(data)))
        for k in range(0, len(data), 4):
            game[address + k] = bytearray(data[k:k + 4])

    rel = files["DLRelocs"]
    if rel[:8] != b"NewerREL":
        raise SystemExit("  DLRelocs: missing NewerREL header")
    table = struct.unpack(">I", rel[8:12])[0]
    targets, pointer_sites, code_sites = set(), [], []
    for i in range(12, table, 8):
        info, location = struct.unpack(">II", rel[i:i + 8])
        rtype, sym = info >> 24, info & 0xFFFFFF
        value = struct.unpack(">I", rel[table + sym * 4:table + sym * 4 + 4])[0]
        target = value if value & 0x80000000 else base + value
        in_module = not location & 0x80000000
        where = base + location if in_module else location
        if not value & 0x80000000:
            targets.add(value)
        if in_module and rtype == 1:
            pointer_sites.append(location)
        if in_module and rtype in (4, 5, 6, 10):
            code_sites.append(location)
        if rtype == 1:
            new = struct.pack(">I", target)
        elif rtype == 4:
            new = struct.pack(">H", target & 0xFFFF)
        elif rtype == 5:
            new = struct.pack(">H", target >> 16)
        elif rtype == 6:
            new = struct.pack(">H", ((target >> 16) + ((target >> 15) & 1)) & 0xFFFF)
        elif rtype == 10:
            old = struct.unpack(">I", bytes(code[location:location + 4]) if in_module else game_read(where, 4))[0]
            new = struct.pack(">I", old | ((target - where) & 0x03FFFFFF))
        else:
            continue  # the loader skips other types too
        if in_module:
            code[location:location + len(new)] = new
        else:
            patches.append((where, new))
            if len(new) == 4:
                game[where] = bytearray(new)
    # Old Kamek links one blob: .text first, then rodata/data. Instruction relocations
    # (HA/LO/REL24) only occur in code and pointer relocations (ADDR32) only in data, so the code
    # ends at the last blr before the first in-module ADDR32 site (NewerSMBW 1.30 US: 0x59484).
    # Every relocation target inside the code part is a function entry (vtable slots, callbacks,
    # hook targets); targets in the data part are data and must not be translated.
    limit = min(pointer_sites) if pointer_sites else max(code_sites, default=len(code) - 4) + 4
    limit = (min(limit, len(code)) + 3) & ~3

    def ends_function(i):
        word = struct.unpack(">I", code[i:i + 4])[0]
        return word == 0x4E800020 or (word >> 26 == 18 and not word & 1)  # blr, or b without link

    text_size = next((i + 4 for i in range(limit - 4, -1, -4) if ends_function(i)), limit)
    seeds = sorted({0} | {t for t in targets if t < text_size and t % 4 == 0})
    print(f"  kamek: DLCode {len(files['DLCode'])} bytes linked at {base:08X} (code 0x{text_size:X} bytes), "
          f"{len(patches)} game patch(es), {len(seeds)} entry point(s)")
    return KamekModule("newer", base, bytes(code), text_size, patches, [base + t for t in seeds])


# ---------------------------------------------------------------- patching

def build_inputs(mod_dir, out_code):
    """Applies everything that can be static to copies of the game binaries.

    Returns (stats, kamek module or None, loader-time patches [(addr, bytes)])."""
    image = GameImage()
    stats = {"baked": 0, "mismatch": 0, "runtime": 0, "unsupported": 0}
    runtime_patches, low_memory = [], {}
    xml_patches = list(memory_patches(mod_dir))
    has_loader = any(p["valuefile"] and p["offset"] == NEWER_LOADER for p in xml_patches)

    for patch in xml_patches:
        value, address = patch["value"], patch["offset"]
        if patch["ocarina"]:
            stats["unsupported"] += 1
            print(f"  ! {patch['source']}: ocarina patch not supported")
            continue
        if patch["search"]:
            found = None
            for section in (s for s in image.sections if s.executable):
                blob = image.read(section.address, section.size)
                i = blob.find(patch["original"])
                while i >= 0 and (section.address + i) % patch["align"]:
                    i = blob.find(patch["original"], i + 1)
                if i >= 0:
                    found = section.address + i
                    break
            if found is None:
                stats["mismatch"] += 1
                continue
            address = found
        current = image.read(address, len(value))
        if current is None:
            # Low memory, .bss, MEM2: the runtime writes these at boot (riivolution_memory.cpp).
            if patch["valuefile"] and address == NEWER_LOADER and has_loader:
                print(f"  = {patch['source']}: Newer loader at {address:08X} is replaced by the native loader")
            else:
                stats["runtime"] += 1
                low_memory[address] = value
            continue
        if patch["original"] and current[:len(patch["original"])] != patch["original"] and current != value:
            stats["mismatch"] += 1
            continue
        section = image.write(address, value)
        stats["baked"] += 1
        kind = "code" if section.executable else "data"
        print(f"  + {patch['source']}: {section.module} {kind} {address:08X} <- {value[:16].hex().upper()}"
              + ("..." if len(value) > 16 else ""))

    kamek = link_newer(mod_dir, image, low_memory) if has_loader else None
    if kamek:
        # The loader runs in the boot scene, after the RELs' static constructors have filled their
        # data (profile tables and objects). Its code patches are baked (constructors never write
        # code); its data patches are applied by the native loader at that same moment, otherwise
        # the constructors would overwrite them with vanilla values.
        loader_data = 0
        for address, value in kamek.game_patches:
            section = image.section_for(address, len(value))
            if section is None or not section.executable:
                runtime_patches.append((address, value))
                loader_data += 1
                continue
            image.write(address, value)
            stats["baked"] += 1
        print(f"  kamek: {stats['baked']} code patch(es) baked in total, {loader_data} data patch(es) "
              "applied by the native loader at run time")
    image.write_files(out_code, (kamek.base, kamek.code, kamek.text_size) if kamek else None)
    return stats, kamek, runtime_patches


def write_function_map(out, kamek):
    path = out / "function_map.txt"
    text = (PORT / "function_map_us.txt").read_text()
    if kamek:
        text += "".join(f"{a:08X} kamek_{kamek.name}_{a:08X}\n" for a in kamek.seeds)
    path.write_text(text)
    return path


def write_mod_source(gen, name, kamek, runtime_patches):
    """C++ the mod build links in: the module image and the patches the native loader applies."""
    mod_dir = gen / "mod"
    mod_dir.mkdir(parents=True, exist_ok=True)
    for old in mod_dir.glob("*.cpp"):
        old.unlink()
    if not kamek and not runtime_patches:
        return
    ident = re.sub(r"\W", "_", name)

    def array(label, data):
        rows = [", ".join(f"0x{b:02X}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
        return f"alignas(32) const uint8_t {label}[] = {{\n    " + ",\n    ".join(rows) + "\n};\n"

    lines = ["// Generated by tools/mod_build.py - do not edit. Contains the mod's code: do not share.",
             '#include "hle/storage/riivolution.h"', "#include <cstdint>", "", "namespace {"]
    if kamek:
        lines.append(array("kModuleImage", kamek.code))
    for i, (_, value) in enumerate(runtime_patches):
        lines.append(array(f"kPatch{i}", value))
    lines += ["struct Register {", "    Register() {"]
    if kamek:
        lines.append(f'        RuntimeRiivolution::RegisterModImage(0x{kamek.base:08X}u, kModuleImage, '
                     f'sizeof(kModuleImage), "{name} ({kamek.name})");')
    for i, (address, _) in enumerate(runtime_patches):
        lines.append(f"        RuntimeRiivolution::RegisterModPatch(0x{address:08X}u, kPatch{i}, sizeof(kPatch{i}));")
    lines += ["    }", "} g_register;", "} // namespace", ""]
    (mod_dir / f"mod_{ident}.cpp").write_text("\n".join(lines))


# ---------------------------------------------------------------- translation

def write_project(src, dst, replacements):
    text = (PORT / src).read_text()
    for old, new in replacements.items():
        text = text.replace(old, new)
    dst.write_text(text)


def translate(out, code_dir, threads, function_map):
    env = dict(os.environ, DOTNET_ROLL_FORWARD="Major")
    common = {
        "workspace_root: .": f"workspace_root: {PORT}",
        '"../../code/main.dol"': f'"{code_dir / "main.dol"}"',
        "native_registration_root: native_us": f"native_registration_root: {PORT / 'native_us'}",
        "native_abi_directories: [native_us]": f"native_abi_directories: [{PORT / 'native_us'}]",
        "path: function_map_us.txt": f"path: {function_map}",
    }
    projects = out / "projects"
    projects.mkdir(parents=True, exist_ok=True)
    write_project("nsmbw_us.yml", projects / "nsmbw_us.yml",
                  dict(common, **{"root: build/nsmbw_us": f"root: {out / 'build' / 'nsmbw_us'}"}))
    rel_ymls = []
    for name in RELS:
        dst = projects / f"nsmbw_us-{name}.yml"
        write_project(f"nsmbw_us-{name}.yml", dst, dict(common, **{
            f'"../../code/rels/{name}.rel"': f'"{code_dir / "rels" / (name + ".rel")}"',
            f"root: build/nsmbw_us-{name}": f"root: {out / 'build' / ('nsmbw_us-' + name)}",
        }))
        rel_ymls.append(str(dst))
    rp = ",".join(rel_ymls)

    def run(args, log):
        print("  $ translator " + " ".join(args[:2]) + " ...")
        with open(out / log, "w") as f:
            subprocess.run(["dotnet", str(TRANSLATOR)] + args, check=True, stdout=f, stderr=subprocess.STDOUT, env=env)

    b = out / "build"
    run(["translate-recursive", hex(DOL_ENTRY), "--project", str(projects / "nsmbw_us.yml"),
         "--outdir", str(b / "nsmbw_us" / "functions"),
         "--output-metadata", str(b / "nsmbw_us" / "base_translation_output.json"), "--threads", str(threads)],
        "translate_dol.log")
    for name, (_, prolog) in RELS.items():
        run(["translate-recursive", hex(prolog), "--project", str(projects / f"nsmbw_us-{name}.yml"),
             "--rel-projects", rp, "--outdir", str(b / f"nsmbw_us-{name}" / "functions"),
             "--output-metadata", str(b / f"nsmbw_us-{name}" / "base_translation_output.json"),
             "--threads", str(threads)], f"translate_{name}.log")
    gen = out / "generated_nsmbw"
    gen.mkdir(exist_ok=True)
    run(["generate-nsmbw-data-init", "--project", str(projects / "nsmbw_us.yml"), "--rel-projects", rp,
         "--out", str(gen / "data_sections_init.cpp"), "--runtime-config-out", str(gen / "RuntimeConfig.h")],
        "datainit.log")
    modules = []
    for line in (PORT / "modules.txt").read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3:
            parts[1] = str(b / Path(parts[1]).relative_to("build"))
            parts[2] = str(b / Path(parts[2]).relative_to("build"))
            modules.append(" ".join(parts))
    (projects / "modules.txt").write_text("\n".join(modules) + "\n")
    run(["emit-nsmbw-build-shards", "--project", str(projects / "nsmbw_us.yml"),
         "--modules-file", str(projects / "modules.txt"), "--native-source-dir", str(PORT / "native_us"),
         "--out", str(gen / "build_shards")], "shards.log")
    return gen


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mod_dir", type=Path)
    ap.add_argument("--name")
    ap.add_argument("--dry-run", action="store_true", help="only list the code patches")
    ap.add_argument("--configure", action="store_true", help="configure a CMake build dir for the mod")
    ap.add_argument("--build", action="store_true", help="configure and build it")
    ap.add_argument("--threads", type=int, default=8)
    args = ap.parse_args()

    mod_dir = args.mod_dir.resolve()
    name = args.name or mod_dir.name
    out = PORT / "mod_builds" / name
    print(f"mod '{name}': {mod_dir}")
    stats, kamek, runtime_patches = build_inputs(mod_dir, out / "code")
    print(f"  baked into main.dol/RELs: {stats['baked']}, other region/version: {stats['mismatch']}, "
          f"left to the runtime (outside the binaries): {stats['runtime']}, unsupported: {stats['unsupported']}"
          + (f", kamek module at {kamek.base:08X} ({len(kamek.code)} bytes)" if kamek else ""))
    if args.dry_run:
        return
    if stats["baked"] == 0 and not kamek:
        print("  nothing to bake: the normal build runs this mod as is")
        return
    function_map = write_function_map(out, kamek)
    gen = translate(out, out / "code", args.threads, function_map)
    write_mod_source(gen, name, kamek, runtime_patches)
    build_dir = UPSTREAM / f"build_mac_mod_{name}"
    configure = ["cmake", "-S", str(UPSTREAM / "runtime"), "-B", str(build_dir), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Release", "-DMKW_BUILD_NSMBW=ON",
                 "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++",
                 f"-DNSMBW_GENERATED_DIR={gen}", f"-DNSMBW_SHARD_MANIFEST={gen / 'build_shards' / 'shards.cmake'}"]
    print("  translated: " + str(gen))
    print("  configure:  " + " ".join(configure))
    if args.configure or args.build:
        subprocess.run(configure, check=True)
    if args.build:
        subprocess.run(["ninja", "-C", str(build_dir), "NSMBWCompiled"], check=True)
        print(f"  built: {build_dir / 'NSMBWCompiled'}")


if __name__ == "__main__":
    main()
