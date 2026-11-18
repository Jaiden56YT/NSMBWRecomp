#!/usr/bin/env python3
"""Refresh per-file sections of ports/wiicompiled/upstream-macos-us.patch from the working tree.

  tools/refresh_upstream_patch.py path/in/NSMBWiicompiled [more paths...]

Each named file's `diff --git` section is replaced with `git diff HEAD -- file` from
third_party/NSMBWiicompiled (or appended if the patch has no section for it yet). Sections for
other files are left alone. Check afterwards with:
  git -C third_party/NSMBWiicompiled apply --check -R ../../ports/wiicompiled/upstream-macos-us.patch
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UP = ROOT / "third_party" / "NSMBWiicompiled"
PATCH = ROOT / "ports" / "wiicompiled" / "upstream-macos-us.patch"


def sections(text):
    parts = re.split(r"(?m)^(?=diff --git )", text)
    head, out = parts[0], []
    for p in parts[1:]:
        m = re.match(r"diff --git a/(\S+) b/", p)
        out.append((m.group(1), p))
    return head, out


def main():
    head, secs = sections(PATCH.read_text())
    for f in sys.argv[1:]:
        tracked = subprocess.run(["git", "-C", str(UP), "ls-files", "--error-unmatch", "--", f],
                                 capture_output=True).returncode == 0
        if tracked or not (UP / f).exists():
            new = subprocess.run(["git", "-C", str(UP), "diff", "HEAD", "--", f],
                                 check=True, capture_output=True, text=True).stdout
        else:
            # Untracked (a file the patch adds): diff it as a new file, with the patch's a/ b/ paths.
            new = subprocess.run(["git", "-C", str(UP), "diff", "--no-index", "--", "/dev/null", f],
                                 capture_output=True, text=True).stdout
        idx = [i for i, (name, _) in enumerate(secs) if name == f]
        if idx:
            if new:
                secs[idx[0]] = (f, new)
            else:
                del secs[idx[0]]
        elif new:
            secs.append((f, new))
        print(f"{'updated' if idx else 'added'} {f}" if new else f"removed {f}")
    PATCH.write_text(head + "".join(s for _, s in secs))


if __name__ == "__main__":
    main()
