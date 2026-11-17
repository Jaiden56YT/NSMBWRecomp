#!/usr/bin/env python3
"""Per-level frame rate from a Switch run's nsmbw_err.log.

  tools/levelfps.py logs/hw/run32/nsmbw_err.log [more logs...]

Each 2 s [interp] window logs "game frames/s=" then "scene=N ... fps=". Consecutive scene=5
windows (inside a level) form one level visit. The first window of a visit includes the load and
is dropped. Prints every visit (windows, average and lowest fps) and, per log, the average over all
level windows, which is the number to compare between builds. Demo levels are replays, so the
same level shows up with the same window count across runs.
"""
import re
import sys

LINE = re.compile(r"\[interp\] scene=(\d+) .*? fps=([\d.]+)")


def visits(path):
    out, cur = [], []
    for line in open(path, errors="replace"):
        m = LINE.search(line)
        if not m:
            continue
        scene, fps = int(m.group(1)), float(m.group(2))
        if scene == 5:
            cur.append(fps)
        elif cur:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return [v[1:] for v in out if len(v) > 2]


def main():
    for path in sys.argv[1:]:
        vs = visits(path)
        allw = [f for v in vs for f in v]
        print(f"== {path}: {len(vs)} level visits, {len(allw)} windows")
        for i, v in enumerate(vs):
            print(f"  visit {i:2d}: {len(v):3d} windows  avg {sum(v)/len(v):5.1f}  min {min(v):5.1f}")
        if allw:
            print(f"  level average {sum(allw)/len(allw):.1f} fps, "
                  f"{100*sum(f < 30 for f in allw)/len(allw):.0f}% of windows under 30")


if __name__ == "__main__":
    main()
