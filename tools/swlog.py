#!/usr/bin/env python3
"""Summarize a Switch run log (sdmc:/switch/nsmbw2/nsmbw_err.log).

Usage: tools/swlog.py nsmbw_err.log [--windows]

Prints the startup facts (CPU clock, audio driver, shader binary cache, env.txt
overrides), then per game scene: time spent, fps (median / worst), spare game
thread time, shaders compiled and audio blocks dropped. --windows also lists
every 2-second stats window. Scene ids are NSMBW profile ids (0 = boot,
3 = world map, 5 = stage: title screen and levels, 10 = game setup);
other ids are printed as numbers.
"""
import re
import statistics
import sys

SCENE_NAMES = {0: "boot", 3: "map", 5: "stage", 10: "setup"}


def main():
    args = sys.argv[1:]
    show_windows = "--windows" in args
    args = [a for a in args if a != "--windows"]
    path = args[0]
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()

    print("== startup")
    last_mesa = None
    for line in lines:
        if line.startswith("[dawn-gl] mesa shader cache: hits"):
            last_mesa = line  # running totals; only the last one matters
            continue
        if (line.startswith("[switch]") or line.startswith("[audio] SDL audio driver")
                or line.startswith("[dawn-gl]")):
            print("  " + line[:160])
    if last_mesa:
        print("  " + last_mesa[:160] + "  (last)")

    windows = []
    cur = None
    for line in lines:
        m = re.match(r"\[nsmbw\]\[interp\] game frames/s=([\d.]+)", line)
        if m:
            cur = {"fps": float(m.group(1)), "idle": None, "compiled": 0, "queued": 0,
                   "audio": 0, "dropped": 0, "scene": None, "p95": None, "late": 0, "maxprod": 0.0}
            windows.append(cur)
            continue
        if cur is None:
            continue
        if m := re.search(r"game thread idle \(spare time\): (\d+) ms per 2 s \((\d+)%\)", line):
            cur["idle"] = int(m.group(2))
        elif m := re.search(r"pipelines: (\d+) compiled, (\d+) queued", line):
            cur["compiled"], cur["queued"] = int(m.group(1)), int(m.group(2))
        elif m := re.search(r"audio blocks: (\d+) queued, (\d+) dropped", line):
            cur["audio"], cur["dropped"] = int(m.group(1)), int(m.group(2))
        elif m := re.search(r"late frames: (\d+), production avg [\d.]+ ms max ([\d.]+) ms", line):
            cur["late"] = int(m.group(1))
            cur["maxprod"] = max(cur["maxprod"], float(m.group(2)))
        elif m := re.search(r"kept-up frames: \d+, production avg [\d.]+ ms max ([\d.]+) ms", line):
            cur["maxprod"] = max(cur["maxprod"], float(m.group(1)))
        elif m := re.match(r"\[nsmbw\]\[interp\] scene=(\d+) .*p95=([\d.]+)ms", line):
            cur["scene"] = int(m.group(1))
            cur["p95"] = float(m.group(2))

    if not windows:
        print("no [nsmbw][interp] stats windows (NSMBW_LOG_INTERP off, or the run ended early)")
        return

    first_play = next((i for i, w in enumerate(windows) if w["fps"] > 1), None)
    print("== %d stats windows (%d s)" % (len(windows), 2 * len(windows)))
    if first_play is not None:
        print("  first frames at ~%d s after the log started counting" % (2 * first_play))

    print("== per scene (consecutive windows grouped)")
    print("  %-8s %6s %8s %8s %8s %9s %9s %9s" % ("scene", "secs", "fps med", "fps min", "idle%", "shaders", "aud drop", "worst ms"))
    groups = []
    for w in windows:
        if groups and groups[-1][0] == w["scene"]:
            groups[-1][1].append(w)
        else:
            groups.append((w["scene"], [w]))
    for scene, ws in groups:
        fps = [w["fps"] for w in ws]
        idle = [w["idle"] for w in ws if w["idle"] is not None]
        name = SCENE_NAMES.get(scene, str(scene))
        print("  %-8s %6d %8.1f %8.1f %8s %9d %9d %9.0f" % (
            name, 2 * len(ws), statistics.median(fps), min(fps),
            ("%.0f" % statistics.median(idle)) if idle else "-",
            sum(w["compiled"] for w in ws), sum(w["dropped"] for w in ws),
            max(w["maxprod"] for w in ws)))

    if show_windows:
        print("== windows")
        for i, w in enumerate(windows):
            print("  %4d s scene %-5s fps %5.1f p95 %6s idle %4s%% shaders %3d queued %3d audio %4d drop %3d late %3d worst %6.1f ms" % (
                2 * i, SCENE_NAMES.get(w["scene"], str(w["scene"])), w["fps"],
                "%.1f" % w["p95"] if w["p95"] is not None else "-",
                w["idle"] if w["idle"] is not None else "-", w["compiled"], w["queued"],
                w["audio"], w["dropped"], w["late"], w["maxprod"]))

    problems = [l for l in lines if re.search(r"(?i)fatal|std::terminate|unavailable at its ordered|abort", l)]
    if problems:
        print("== problems")
        for l in problems[:15]:
            print("  " + l[:200])


if __name__ == "__main__":
    main()
