#!/usr/bin/env python3
"""Write a tiny Kamek 2 test mod for tools/mod_build.py (hand-assembled, no compiler needed).

Layout, like a mod built with Treeki's Kamek and its -output-riiv loader:

  <out>/riivolution/KamekTest2.xml   loader code written to low memory with <memory value=...>,
                                     a boot-table entry pointed at it, and a folder patch for /Code
  <out>/KamekTest2/Code/E2.bin       the Kamek binary (format version 2) for US Rev 2 ("E2")

The binary's code hooks fBase_c::createRoot (US 0x80162B20) with a Branch command, counts calls
in a .bss counter and sets player 1's lives (daPyMng_c::mRest, US 0x80354E90) to counter - 60.
A static constructor (listed in the binary's ctor range) sets the counter to 100 first, so lives
read 41, 42, ... only if the constructor ran. A CondWrite32 renames the save from wiimj2d to
KMK2j2d (only where the original word matches). Relocations cover Addr32, Addr16Ha/Lo and Rel24.

Usage: python3 -I tools/tests/make_kamek2_testmod.py <out dir>
"""
import struct
import sys
from pathlib import Path

CREATE_ROOT = 0x80162B20
CREATE_ROOT_FIRST = 0x7C802378   # mr r0, r4
M_REST = 0x80354E90
SAVE_NAME = 0x802F118C           # "wiimj2d" in main.dol .rodata (US)
LOADER = 0x80001900


def lis(rd, imm): return (15 << 26) | (rd << 21) | (imm & 0xFFFF)
def li(rd, imm): return (14 << 26) | (rd << 21) | (imm & 0xFFFF)
def addi(rd, ra, imm): return (14 << 26) | (rd << 21) | (ra << 16) | (imm & 0xFFFF)
def lwz(rd, d, ra): return (32 << 26) | (rd << 21) | (ra << 16) | (d & 0xFFFF)
def stw(rs, d, ra): return (36 << 26) | (rs << 21) | (ra << 16) | (d & 0xFFFF)
BLR = 0x4E800020
B = 0x48000000

ADDR32, LO, HI, HA, REL24, WRITE32, CONDWRITE32, BRANCH = 1, 4, 5, 6, 10, 32, 36, 64


def build():
    code = [
        lis(11, 0), lwz(12, 0, 11), addi(12, 12, 1), stw(12, 0, 11),   # 0x00 counter++
        addi(12, 12, -60), lis(11, 0), stw(12, 0, 11),                  # 0x10 lives = counter - 60
        CREATE_ROOT_FIRST, B,                                           # 0x1C replaced insn, 0x20 back
        lis(11, 0), li(12, 100), stw(12, 0, 11), BLR,                   # 0x24 ctor: counter = 100
    ]
    blob = b"".join(struct.pack(">I", w) for w in code)
    blob += b"\0" * (0x40 - len(blob))
    blob += struct.pack(">I", 0)            # 0x40: ctor table (one entry, filled by Addr32)
    code_size, bss = len(blob), 4           # counter = first .bss word, at code_size
    counter = code_size

    def cmd(cid, addr, *args, absolute=False):
        if absolute:
            return struct.pack(">II", (cid << 24) | 0xFFFFFE, addr) + struct.pack(f">{len(args)}I", *args)
        return struct.pack(">I", (cid << 24) | addr) + struct.pack(f">{len(args)}I", *args)

    commands = b"".join([
        cmd(HA, 0x02, counter), cmd(LO, 0x06, counter), cmd(LO, 0x0E, counter),
        cmd(HA, 0x16, M_REST), cmd(LO, 0x1A, M_REST),
        cmd(REL24, 0x20, CREATE_ROOT + 4),
        cmd(HA, 0x26, counter), cmd(LO, 0x2E, counter),
        cmd(ADDR32, 0x40, 0x24),
        cmd(BRANCH, CREATE_ROOT, 0x00, absolute=True),
        cmd(CONDWRITE32, SAVE_NAME, 0x4B4D4B32, 0x7769696D, absolute=True),  # "KMK2" if "wiim"
    ])
    header = b"Kamek\0\0\x02" + struct.pack(">IIIIII", bss, code_size, 0x40, 0x44, 0, 0)
    return header + blob + commands


XML = """<wiidisc version="1" root="/KamekTest2">
  <id game="SMN"><region type="E" /></id>
  <options>
    <section name="Kamek 2 Test">
      <option name="Kamek 2 Test">
        <choice name="Enabled"><patch id="k2" /></choice>
      </option>
    </section>
  </options>
  <patch id="k2">
    <folder external="Code" disc="/Code" create="true" />
    <!-- loader (stand-in: li r3,1 / blr, padded), as Kamek's -output-riiv writes it -->
    <memory offset="0x%08X" value="386000014E80002060000000600000006000000060000000" />
    <!-- run it in the boot sequence: US boot-table entry 20 (as NewerSMBW 1.30 does) -->
    <memory offset="0x80328130" value="%08X" original="8015BB20" />
  </patch>
</wiidisc>
""" % (LOADER, LOADER)


def main():
    out = Path(sys.argv[1])
    (out / "KamekTest2" / "Code").mkdir(parents=True, exist_ok=True)
    (out / "riivolution").mkdir(parents=True, exist_ok=True)
    (out / "KamekTest2" / "Code" / "E2.bin").write_bytes(build())
    (out / "riivolution" / "KamekTest2.xml").write_text(XML)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
