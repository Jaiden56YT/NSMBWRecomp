# Switch tests for tonight (2026-11-14)

Plug the SD card into the Mac and tell Claude "card's back in": it copies both programs over and
ejects the card. Then on the Switch:

## 1. Dawn probe (graphics-library test, ~10 seconds)
Homebrew Menu -> **Dawn probe**. Expected: a yellow triangle on a blue background that slowly turns
teal, then it returns to the menu by itself (or press +).
Writes `switch/dawnprobe/probe.log`.

## 2. NSMBW Recomp 2 (the new, faster build)
Homebrew Menu -> **NSMBW Recomp 2**. Best launched with full memory: hold R while starting any
game, then pick it from the Homebrew Menu that opens.
It uses the game data already on the card (`switch/nsmbw/extracted`), keeps its own save and
settings in `switch/nsmbw2/`, and writes `switch/nsmbw2/nsmbw.log`.
The first start compiles shaders and can sit on a black screen for a while.
Controls (Joy-Con/Pro Controller, by position): bottom button = jump, left button = run/fireball,
shoulders = spin, + = pause, - twice = skip a cutscene.

## Afterwards
Plug the card back into the Mac so Claude can read both logs. If either one freezes, hold the
power button and restart; nothing on the card is at risk (saves for the new build are separate).
The old build (NSMBW Recomp) is unchanged and still there.
