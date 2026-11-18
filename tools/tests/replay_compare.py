#!/usr/bin/env python3
"""Compare two NSMBW_REPLAY_HASHES files (online play step 0, docs/online-play.md).

Usage: replay_compare.py A.hashes B.hashes [--ignore 0x80000000-0x80040000,...] [--top 40]

Frames are matched by game-frame number. Reports the first frame where any 256 KiB page of guest
memory differs, the pages that differ there, and for every page that ever differs: the first frame,
how many frames it differs, and whether it was still different at the end. Pages listed with
--ignore (address ranges) are left out, so known host-written areas can be excluded.
"""
import struct
import sys


def load(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"NSMBWHS1":
        sys.exit(f"{path}: not a hash file")
    page, n1, n2 = struct.unpack_from("<3I", data, 8)
    n = n1 + n2
    rec = 12 + 4 * n
    frames = {}
    off = 20
    while off + rec <= len(data):
        frame, tick, scene = struct.unpack_from("<3I", data, off)
        frames[frame] = (tick, scene, struct.unpack_from(f"<{n}I", data, off + 12))
        off += rec
    return page, n1, n2, frames


def page_addr(i, page, n1):
    return 0x80000000 + i * page if i < n1 else 0x90000000 + (i - n1) * page


def main():
    args = sys.argv[1:]
    ignore = []
    top = 40
    if "--ignore" in args:
        k = args.index("--ignore")
        for r in args[k + 1].split(","):
            a, b = r.split("-")
            ignore.append((int(a, 16), int(b, 16)))
        del args[k:k + 2]
    if "--top" in args:
        k = args.index("--top")
        top = int(args[k + 1])
        del args[k:k + 2]
    pa, n1, n2, fa = load(args[0])
    pb, m1, m2, fb = load(args[1])
    assert (pa, n1, n2) == (pb, m1, m2), "different page layouts"
    common = sorted(set(fa) & set(fb))
    print(f"frames: A {len(fa)}, B {len(fb)}, common {len(common)}")
    if not common:
        return

    def ignored(i):
        a = page_addr(i, pa, n1)
        return any(lo <= a < hi for lo, hi in ignore)

    first_diff = {}
    count = {}
    last_frame = common[-1]
    first = None
    for fr in common:
        ta, sa, ha = fa[fr]
        tb, sb, hb = fb[fr]
        diff = [i for i in range(len(ha)) if ha[i] != hb[i] and not ignored(i)]
        if diff and first is None:
            first = fr
            print(f"first difference at frame {fr} (A tick {ta} scene {sa}, B tick {tb} scene {sb}):")
            for i in diff[:top]:
                print(f"  page 0x{page_addr(i, pa, n1):08X}")
        for i in diff:
            first_diff.setdefault(i, fr)
            count[i] = count.get(i, 0) + 1
    if first is None:
        print("no differences: the two runs match on every common frame")
        return
    end_diff = set(i for i in range(len(fa[last_frame][2])) if fa[last_frame][2][i] != fb[last_frame][2][i])
    print(f"pages that ever differ: {len(first_diff)} (A/B at last common frame {last_frame}: scene "
          f"{fa[last_frame][1]}/{fb[last_frame][1]}, still different: {len(end_diff - set(i for i in end_diff if ignored(i)))})")
    for i in sorted(first_diff, key=lambda i: (first_diff[i], i))[:top]:
        print(f"  0x{page_addr(i, pa, n1):08X}  first frame {first_diff[i]:6d}  differs in {count[i]:6d} frames"
              f"{'  (still at end)' if i in end_diff else ''}")
    # Scene timeline: where the two runs' scenes disagree first.
    for fr in common:
        if fa[fr][1] != fb[fr][1]:
            print(f"scenes first disagree at frame {fr}: A {fa[fr][1]}, B {fb[fr][1]}")
            break
    else:
        print("scenes agree on every common frame")


if __name__ == "__main__":
    main()
