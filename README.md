# morph

Homebrew launcher for Nintendo Switch (Horizon): a grid of homebrew, a settings menu, and
HOME-screen shortcuts (forwarders) for any NRO.

## Build

Requires devkitPro (devkitA64, libnx, deko3d, elf2nso, npdmtool).

    cmake -B build -S .
    cmake --build build -j

Output: `build/morph.nro`.

## Forwarder shortcuts

Shortcuts need `sdmc:/switch/prod.keys`. One press on a "Create forwarder" entry builds the
forwarder into `sdmc:/config/morph/forwarder/<tid>/` and installs it to HOME.
Only install shortcuts you actually use: they write titles to NAND/SD.

## Controls

- Left stick or D-pad: move the cursor
- ZL / ZR, or right stick left/right: previous / next page
- A: launch the selected homebrew
- R3: star or unstar; starred homebrew move to the front of the grid
- -: settings menu
- B or +: close the menu, or exit morph
- Touch: tap to select (tap again to launch), swipe left/right to change page

In the settings menu, A on "Background color" opens the color picker. The left stick sets
saturation and brightness, the right stick sets hue, A types a HEX value with the Switch
keyboard, and B closes it.

## Known issues

- 30 seconds of black screen when exiting to HOME via `+` (rare). Seen when starting morph after
  changing the system language and rebooting. If it happens again, please send
  `sdmc:/config/morph/log.txt` and describe what happened before the failure.

## Licence

GPL-3.0-or-later. See LICENSE and NOTICE.md.
