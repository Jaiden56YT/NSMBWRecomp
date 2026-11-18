#!/usr/bin/env python3
"""Write a tiny Newer-style Kamek test mod for the mod pipeline (tools/mod_build.py).

There is no PowerPC compiler on this machine, so the mod's code is hand-assembled. It has
the same shape as NewerSMBW's distribution:

  <out>/riivolution/KamekTest.xml      loader patches + a folder patch for /NewerRes
  <out>/KamekTest/Loader.bin           stand-in for Newer's Loader.bin (valuefile at 0x80001800)
  <out>/KamekTest/NewerRes/SystemUS_2.bin    static patches (Kamek System format)
  <out>/KamekTest/NewerRes/DLCodeUS_2.bin    position-independent code + data
  <out>/KamekTest/NewerRes/DLRelocsUS_2.bin  "NewerREL" relocations + symbol table

What the code does: a kmBranch-style hook on fBase_c::createRoot (US 0x80162B20, first
instruction `mr r0,r4`) jumps into DLCode, which increments a counter in DLCode's own data,
stores 40 + counter into daPyMng_c::mRest[0] (US 0x80354E90, player 1's lives), runs the
replaced instruction and branches back. On the HUD Mario's lives read x4N in every level.
It exercises REL24 (game -> DLCode and DLCode -> game), ADDR16_HA/LO against a DLCode symbol
and against a game address. System.bin also carries the ArchiveHeap code patch
(0x800E4994: lis r3,0x120 -> lis r3,0x140) that NewerSMBW makes.

The XML uses NewerSMBW's (NSMASR's) US boot-table patch: the boot scene's state table at
0x803280E0 is rewritten so the REL link phases run first and then calls the loader at 0x80001800.

Usage: python3 -I tools/tests/make_kamek_testmod.py <out dir>
"""
import struct
import sys
from pathlib import Path

CREATE_ROOT = 0x80162B20      # fBase_c::createRoot (US)
CREATE_ROOT_FIRST = 0x7C802378  # mr r0, r4 (checked against the US main.dol)
M_REST = 0x80354E90           # daPyMng_c::mRest (PAL 0x80355190 through the PAL->US table)
ARCHIVE_HEAP_PATCH = (0x800E4994, 0x3C600140)

R_ADDR32, R_LO, R_HI, R_HA, R_REL24 = 1, 4, 5, 6, 10


def lis(rd, imm): return (15 << 26) | (rd << 21) | (imm & 0xFFFF)
def addi(rd, ra, imm): return (14 << 26) | (rd << 21) | (ra << 16) | (imm & 0xFFFF)
def lwz(rd, d, ra): return (32 << 26) | (rd << 21) | (ra << 16) | (d & 0xFFFF)
def stw(rs, d, ra): return (36 << 26) | (rs << 21) | (ra << 16) | (d & 0xFFFF)
B = 0x48000000  # b with LI = 0, filled in by a REL24 relocation


def build():
    counter = 0x40
    code = [
        lis(11, 0),            # 0x00  HA counter
        lwz(12, 0, 11),        # 0x04  LO counter
        addi(12, 12, 1),       # 0x08
        stw(12, 0, 11),        # 0x0C  LO counter
        addi(12, 12, 40),      # 0x10
        lis(11, 0),            # 0x14  HA mRest
        stw(12, 0, 11),        # 0x18  LO mRest
        CREATE_ROOT_FIRST,     # 0x1C  the instruction the hook replaced
        B,                     # 0x20  REL24 -> createRoot + 4
    ]
    dlcode = b"".join(struct.pack(">I", w) for w in code)
    dlcode += b"\0" * (counter - len(dlcode)) + struct.pack(">I", 0) + b"\0" * 12

    # symbols: bit 31 set = game address, otherwise an offset into DLCode
    symbols = [counter, M_REST, CREATE_ROOT + 4, 0x0]
    relocs = [
        (R_HA, 0x02, 0), (R_LO, 0x06, 0), (R_LO, 0x0E, 0),
        (R_HA, 0x16, 1), (R_LO, 0x1A, 1),
        (R_REL24, 0x20, 2),
        (R_REL24, CREATE_ROOT, 3),   # the hook itself: game code -> DLCode + 0
    ]
    body = b"".join(struct.pack(">II", (t << 24) | s, loc) for t, loc, s in relocs)
    dlrelocs = struct.pack(">8sI", b"NewerREL", 12 + len(body)) + body
    dlrelocs += b"".join(struct.pack(">I", s) for s in symbols)

    # System.bin: single-word patches (address has bit 31 set), then the end marker
    system = struct.pack(">II", CREATE_ROOT, B)  # the loader ORs the REL24 into this word
    system += struct.pack(">II", *ARCHIVE_HEAP_PATCH)
    system += struct.pack(">I", 0xFFFFFFFF)
    return dlcode, dlrelocs, system


XML = """<wiidisc version="1">
  <id game="SMN">
    <region type="E" />
  </id>
  <options>
    <section name="Kamek Test">
      <option name="Kamek Test">
        <choice name="Enabled"><patch id="kamek" /></choice>
      </option>
    </section>
  </options>
  <patch id="kamek">
    <folder external="/KamekTest/NewerRes" disc="/NewerRes" create="true" />
    <memory offset="0x80001800" valuefile="/KamekTest/Loader.bin" />
    <!-- NewerSMBW / NSMASR US boot-table patch: REL link phases first, then the loader -->
    <memory offset="0x803280E0" value="8015B970 8015B9C0 8015BA10 8015BA60 8015BAF0 80001800 8015BBF0 800E50D0 800E5110 8015C010 8015C100 8015BCB0 8015C070 8015BE20 8015C090 8015C0E0 8015BD40 8015BD80 8015BDD0 8015BE60 8015BEE0" original="8015BBF0" />
  </patch>
</wiidisc>
"""


def main():
    out = Path(sys.argv[1])
    res = out / "KamekTest" / "NewerRes"
    res.mkdir(parents=True, exist_ok=True)
    (out / "riivolution").mkdir(parents=True, exist_ok=True)
    dlcode, dlrelocs, system = build()
    (res / "DLCodeUS_2.bin").write_bytes(dlcode)
    (res / "DLRelocsUS_2.bin").write_bytes(dlrelocs)
    (res / "SystemUS_2.bin").write_bytes(system)
    # Stand-in loader: never executed (the runtime replaces 0x80001800 with a native loader).
    (out / "KamekTest" / "Loader.bin").write_bytes(struct.pack(">I", 0x38600001) + struct.pack(">I", 0x4E800020) * 63)
    (out / "riivolution" / "KamekTest.xml").write_text(XML)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
