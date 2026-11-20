#!/usr/bin/env python3
"""Write a Riivolution test pack for partial file patches (<file offset fileoffset length>).

  python3 -I tools/tests/make_partial_testmod.py <extracted disc folder> <mods folder>

Writes <mods>/PartialTest/ with four file patches, applied in order:
  1. /Stage/01-01.arc: 0x200 zero bytes at 0x100 (would break the level on its own)
  2. /Stage/01-01.arc: the original bytes 0x100..0x300 back, taken from 0x40 into the external
     file (fileoffset + length), so the result equals the vanilla file and 1-1 still loads
  3. /PartialTest/new.bin (create): 13 bytes at 0x10 of a new file, so 16 zero bytes then the text
  4. /Stage/01-02.arc: offset past the end with resize="false", so the file is unchanged
The runtime logs "partial patch" per entry; the spliced files are in $TMPDIR/nsmbw-riivo-partial-*
while the game runs. The disc's own bytes are only read here, never copied into the repo.
"""
import sys
from pathlib import Path

XML = """<wiidisc version="1">
  <id game="SMN"><region type="E" /></id>
  <options><section name="Partial"><option name="Partial"><choice name="Enabled"><patch id="p" /></choice></option></section></options>
  <patch id="p" root="/PartialTest">
    <file disc="/Stage/01-01.arc" external="data/zeros.bin" offset="0x100" />
    <file disc="/Stage/01-01.arc" external="data/restore.bin" offset="0x100" fileoffset="0x40" length="0x200" />
    <file disc="/PartialTest/new.bin" external="data/new.bin" offset="0x10" create="true" />
    <file disc="/Stage/01-02.arc" external="data/new.bin" offset="0x7FFFFFF0" resize="false" />
  </patch>
</wiidisc>
"""


def main():
    disc, mods = Path(sys.argv[1]), Path(sys.argv[2])
    original = (disc / "files" / "Stage" / "01-01.arc").read_bytes()
    pack = mods / "PartialTest"
    data = pack / "PartialTest" / "data"
    data.mkdir(parents=True, exist_ok=True)
    (pack / "riivolution").mkdir(parents=True, exist_ok=True)
    (data / "zeros.bin").write_bytes(b"\0" * 0x200)
    (data / "restore.bin").write_bytes(b"\xAA" * 0x40 + original[0x100:0x300])
    (data / "new.bin").write_bytes(b"HELLO-PARTIAL")
    (pack / "riivolution" / "PartialTest.xml").write_text(XML)
    print(f"wrote {pack}")


if __name__ == "__main__":
    main()
