#!/usr/bin/env python3
"""Symbolize the Switch sampling profile (sdmc:/switch/nsmbw2/profile.txt).

Usage: tools/swprof.py profile.txt [elf] [--top N]

profile.txt lines: "main=0x..." then per slot "slot S total T" and
"S count pc lr caller". Slot 0 = game thread, slot 1 = aurora frame worker.
Prints, per slot, the hottest functions (self time), the hottest callers of the
top functions (via lr / frame-chain return address) and the share in waits.
"""
import collections
import subprocess
import sys

A2L = "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-addr2line"
NM = "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm"
DEFAULT_ELF = "third_party/NSMBWiicompiled/build_switch/NSMBWCompiled.elf"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    top = 40
    if "--top" in sys.argv:
        top = int(sys.argv[sys.argv.index("--top") + 1])
        args = [a for a in args if a != str(top)]
    prof = args[0]
    elf = args[1] if len(args) > 1 else DEFAULT_ELF

    main_link = None
    for line in subprocess.run([NM, elf], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "main":
            main_link = int(parts[0], 16)
    if main_link is None:
        sys.exit("main not found in ELF")

    base = None
    totals = {}
    rows = []
    for line in open(prof):
        line = line.strip()
        if line.startswith("main="):
            base = int(line[5:], 16) - main_link
        elif line.startswith("slot "):
            p = line.split()
            totals[int(p[1])] = int(p[3])
        elif line:
            s, c, pc, lr, caller = line.split()
            rows.append((int(s), int(c), int(pc, 16), int(lr, 16), int(caller, 16)))
    if base is None:
        sys.exit("no main= line")

    addrs = set()
    for _, _, pc, lr, caller in rows:
        for a in (pc, lr, caller):
            if a:
                addrs.add(a - base)
    addrs = sorted(a for a in addrs if 0 <= a < 1 << 32)
    out = subprocess.run([A2L, "-f", "-C", "-e", elf] + [hex(a) for a in addrs],
                         capture_output=True, text=True).stdout.splitlines()
    names = {}
    for i, a in enumerate(addrs):
        fn = out[2 * i] if 2 * i < len(out) else "??"
        names[a] = fn

    def name(a):
        if not a:
            return "-"
        n = names.get(a - base, "??")
        if n == "??":
            return "?%#x" % (a - base)
        return n if len(n) < 110 else n[:107] + "..."

    for slot in sorted(totals):
        tot = totals[slot] or 1
        label = {0: "game thread", 1: "aurora frame worker"}.get(slot, "slot %d" % slot)
        print("=" * 100)
        print("%s: %d samples (sampled rows cover %d)" %
              (label, totals[slot], sum(c for s, c, *_ in rows if s == slot)))
        self_t = collections.Counter()
        callers = collections.defaultdict(collections.Counter)
        for s, c, pc, lr, caller in rows:
            if s != slot:
                continue
            f = name(pc)
            self_t[f] += c
            # For a leaf, lr is the caller; otherwise the frame chain gives it.
            callers[f][name(lr) + "  <=  " + name(caller)] += c
        print("\n-- self time --")
        for f, c in self_t.most_common(top):
            print("%6.2f%%  %6d  %s" % (100.0 * c / tot, c, f))
        print("\n-- callers of the top 12 (lr <= frame-chain caller) --")
        for f, c in self_t.most_common(12):
            print("%s  (%.1f%%)" % (f, 100.0 * c / tot))
            for k, kc in callers[f].most_common(4):
                print("      %5.1f%%  %s" % (100.0 * kc / tot, k))


if __name__ == "__main__":
    main()
