# Pointdevice Mode (TH15-style checkpoints) for th06

This branch (`feat/pointdevice-mode`, based on `web-port-compressed-audio`)
adds a Pointdevice Mode like the one in Touhou 15. When you get hit you go
back to the last checkpoint instead of losing a life, as many times as you need.

## How it plays

- **On by default.** On the title screen press **P** to turn it on or off.
  The current setting shows at the bottom left.
- **Checkpoints** are taken automatically:
  - at the start of every stage,
  - at the start of every boss attack (spell cards and non-spells),
  - every 15 seconds during the stage, but only at a calm moment (no bullets
    close to you, no lasers, no bomb or dialogue in progress).
  "Checkpoint" flashes at the bottom of the play area when one is taken.
- **When you get hit** the game freezes and asks:
  - **Continue from checkpoint**: everything (score, power, bombs, enemies,
    bullets, boss HP, music) goes back to the checkpoint, and you get 2 seconds
    of invulnerability.
  - **Give up**: ends the run and shows the result screen.
- **Pause (ESC)** opens the Pointdevice pause menu:
  - **Resume** (ESC also resumes)
  - **Save and quit**: saves your difficulty, character and shot type, the
    stage you are on, and the score, power, bombs and retry count that stage
    started with, then returns to the title screen.
  - **Quit without saving**
- **Resuming:** the title screen shows `Saved run: Stage N ... (R)`. Press **R**
  to carry on from the start of that stage. A saved run is used up when you
  resume it; save and quit again to keep a new one.
- **Extends give a bomb** instead of a life.
- **Records stay clean:** Pointdevice runs never save a replay and never write
  `score.dat`.

Menus: Up/Down to move, Z to pick. They ignore input for the first third of a
second, so mashing Z through a death never picks an option by accident.

## Where the saved run is stored

- Web build: the browser's `localStorage`, key `th06.pointdevice.sav`
  (this web build keeps its files in memory only, so a file would not survive
  a reload). Cleared if the player clears site data.
- Desktop: `pointdevice.sav` in the working directory, next to `score.dat`.

## Building for the web

Same as before (see `web/README.md`):

```bash
source ~/emsdk/emsdk_env.sh
export TH06_ASSETS_DIR=~/th06-assets-ogg
bash web/build_web.sh
cd dist && python3 -m http.server 8000
```

Open `http://localhost:8000/`. `web/build_web.sh` now also compiles
`src/PointDevice.cpp`.

## Building for desktop

```bash
premake5 gmake && cd build && make -j10
```

Run `./th06` from a folder with the `紅魔郷*.DAT` files and `msgothic.ttc`.

## Test helpers (desktop only)

- `TH06_POINTDEVICE=0` or `--legacy` starts with the mode off; `--pointdevice` forces it on.
- `TH06_POINTDEVICE_LOG=1` prints checkpoint, death, rewind, save and resume events to stderr.
- `TH06_POINTDEVICE_AUTOTEST=1` starts an autopilot that clicks through the menus,
  plays Stage 1 and picks Continue after every death. `=giveup` gives up at the
  first death; `=savequit` plays on (always invulnerable), saves and quits early
  in Stage 2, then resumes the saved run from the title screen; `=hold` leaves
  the first menu open so you can look at it.
