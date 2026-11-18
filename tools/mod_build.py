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


# ---------------------------------------------------------------- patching

def apply_code_patches(mod_dir, out_code):
    out_code.mkdir(parents=True, exist_ok=True)
    (out_code / "rels").mkdir(exist_ok=True)
    files = {"main.dol": bytearray((REPO / "code" / "main.dol").read_bytes())}
    sections = dol_sections(REPO / "code" / "main.dol")
    for name, (base, _) in RELS.items():
        files[name] = bytearray((REPO / "code" / "rels" / f"{name}.rel").read_bytes())
        sections += rel_sections(REPO / "code" / "rels" / f"{name}.rel", name, base)

    def read(section, address, length):
        start = section.file_offset + address - section.address
        return bytes(files[section.module][start:start + length])

    stats = {"code": 0, "data": 0, "mismatch": 0, "outside": 0, "unsupported": 0}
    for patch in memory_patches(mod_dir):
        value, address = patch["value"], patch["offset"]
        if patch["ocarina"]:
            stats["unsupported"] += 1
            print(f"  ! {patch['source']}: ocarina patch not supported")
            continue
        if patch["search"]:
            found = None
            for section in (s for s in sections if s.executable):
                blob = read(section, section.address, section.size)
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
        section = next((s for s in sections if s.contains(address, len(value))), None)
        if section is None:
            stats["outside"] += 1
            what = f"valuefile {Path(patch['valuefile']).name}" if patch["valuefile"] else f"{len(value)} bytes"
            print(f"  ! {patch['source']}: {address:08X} ({what}) is outside main.dol/RELs: "
                  "new code there needs an extra translated module (not done yet)")
            continue
        if not section.executable:
            stats["data"] += 1  # the runtime applies data patches at boot
            continue
        current = read(section, address, len(value))
        if patch["original"] and current != patch["original"][:len(current)] and current != value:
            stats["mismatch"] += 1
            continue
        start = section.file_offset + address - section.address
        files[section.module][start:start + len(value)] = value
        stats["code"] += 1
        print(f"  + {patch['source']}: {section.module} {address:08X} <- {value.hex().upper()}")

    (out_code / "main.dol").write_bytes(files["main.dol"])
    for name in RELS:
        (out_code / "rels" / f"{name}.rel").write_bytes(files[name])
    return stats


# ---------------------------------------------------------------- translation

def write_project(src, dst, replacements):
    text = (PORT / src).read_text()
    for old, new in replacements.items():
        text = text.replace(old, new)
    dst.write_text(text)


def translate(out, code_dir, threads):
    env = dict(os.environ, DOTNET_ROLL_FORWARD="Major")
    common = {
        "workspace_root: .": f"workspace_root: {PORT}",
        '"../../code/main.dol"': f'"{code_dir / "main.dol"}"',
        "native_registration_root: native_us": f"native_registration_root: {PORT / 'native_us'}",
        "native_abi_directories: [native_us]": f"native_abi_directories: [{PORT / 'native_us'}]",
        "path: function_map_us.txt": f"path: {PORT / 'function_map_us.txt'}",
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
    stats = apply_code_patches(mod_dir, out / "code")
    print(f"  code patches baked in: {stats['code']}, data patches (left to the runtime): {stats['data']}, "
          f"other region/version: {stats['mismatch']}, outside known sections: {stats['outside']}, "
          f"unsupported: {stats['unsupported']}")
    if args.dry_run:
        return
    if stats["code"] == 0:
        print("  no code patches: the normal build runs this mod as is")
        return
    gen = translate(out, out / "code", args.threads)
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
