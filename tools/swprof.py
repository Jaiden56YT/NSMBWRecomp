#!/usr/bin/env python3
"""Symbolize Switch sampling profiles (sdmc:/switch/nsmbw2/profile_NN.txt).

Usage: tools/swprof.py ELF profile_NN.txt [more windows...] [--top N]

Each file: "main=0x...", "window W uptime T s", per slot "slot S total T" and
rows "S count pc lr ret1 ret2 ret3 ret4" (older files: "S count pc lr caller").
Slot 0 = game thread, slot 1 = aurora frame worker. Several windows are summed.
Prints per slot: self time, inclusive time (function anywhere in the sampled
stack) and the stacks of the top self-time functions.
"""
import collections
import subprocess
import sys

A2L = "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-addr2line"
NM = "/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm"


def main():
    argv = sys.argv[1:]
    top = 40
    if "--top" in argv:
        i = argv.index("--top")
        top = int(argv[i + 1])
        del argv[i:i + 2]
    elf, files = argv[0], argv[1:]

    main_link = None
    for line in subprocess.run([NM, elf], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "main":
            main_link = int(parts[0], 16)
    if main_link is None:
        sys.exit("main not found in ELF")

    totals = collections.Counter()
    rows = []  # (slot, count, [addresses relative to the ELF])
    for path in files:
        base = None
        for line in open(path):
            line = line.strip()
            if line.startswith("main="):
                base = int(line[5:], 16) - main_link
            elif line.startswith("window"):
                print("#", path, line)
            elif line.startswith("slot "):
                p = line.split()
                totals[int(p[1])] += int(p[3])
            elif line:
                p = line.split()
                addrs = [int(a, 16) for a in p[2:]]
                rows.append((int(p[0]), int(p[1]), [a - base if a else 0 for a in addrs]))

    uniq = sorted({a for _, _, addrs in rows for a in addrs if 0 < a < 1 << 32})
    names = {}
    chunk = 4000
    for i in range(0, len(uniq), chunk):
        part = uniq[i:i + chunk]
        out = subprocess.run([A2L, "-f", "-C", "-e", elf] + [hex(a) for a in part],
                             capture_output=True, text=True).stdout.splitlines()
        for j, a in enumerate(part):
            names[a] = out[2 * j] if 2 * j < len(out) else "??"

    def name(a):
        if not a:
            return "-"
        n = names.get(a, "??")
        if n == "??":
            return "?%#x" % a
        return n if len(n) < 100 else n[:97] + "..."

    for slot in sorted(totals):
        tot = totals[slot] or 1
        label = {0: "game thread", 1: "aurora frame worker"}.get(slot, "slot %d" % slot)
        print("=" * 100)
        print("%s: %d samples" % (label, totals[slot]))
        self_t = collections.Counter()
        incl = collections.Counter()
        stacks = collections.defaultdict(collections.Counter)
        for s, c, addrs in rows:
            if s != slot:
                continue
            fns = [name(a) for a in addrs]
            self_t[fns[0]] += c
            for f in set(f for f in fns if f != "-"):
                incl[f] += c
            stacks[fns[0]][" <= ".join(f for f in fns[1:] if f != "-")] += c
        print("\n-- self time --")
        for f, c in self_t.most_common(top):
            print("%6.2f%%  %s" % (100.0 * c / tot, f))
        print("\n-- inclusive (anywhere in the sampled stack) --")
        for f, c in incl.most_common(top):
            print("%6.2f%%  %s" % (100.0 * c / tot, f))
        print("\n-- stacks of the top 10 self-time functions --")
        for f, c in self_t.most_common(10):
            print("%s  (%.1f%%)" % (f, 100.0 * c / tot))
            for k, kc in stacks[f].most_common(4):
                print("      %5.1f%%  %s" % (100.0 * kc / tot, k[:220]))


if __name__ == "__main__":
    main()
