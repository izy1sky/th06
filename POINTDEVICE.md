# Pointdevice Mode (TH15-style checkpoints) for th06

This repository adds a Pointdevice Mode like the one in Touhou 15 (building it: see the [README](README.md)). When you get hit you go
back to the last checkpoint instead of losing a life, as many times as you need.

## How it plays

- **Choosing the mode:** after you pick difficulty, character and shot type,
  the game asks for **Pointdevice Mode** or **Legacy Mode** (the original
  rules), styled like the difficulty screen (two cards: Pointdevice and Legacy), with a short description of each. The hit and pause menus
  use the look of the game's own pause menu. Menus fade and slide in, and
  the selection cross-fades as you move the cursor. The art comes from
  `tools/pointdevice-art/` (see its README to change it). Pointdevice is preselected the
  first time; after that the game remembers your last choice.
- **Lives are not used:** in a Pointdevice run the side panel shows no lives,
  1UPs and extends give a bomb instead, and stage clears give no life bonus.
- **Checkpoints** are taken automatically:
  - at the start of every stage,
  - at the start and at the end of every boss and midboss attack (spell
    cards and non-spells), so dying to the enemies after a midboss doesn't
    send you back into its attack,
  - every 15 seconds outside boss and midboss attacks, but only at a calm
    moment (no bullets close to you, no lasers, no bomb or dialogue in
    progress). During an attack there are none, so dying always sends you
    back to the attack's start.
  "Checkpoint" flashes at the bottom of the play area when one is taken.
- **When you get hit** the game freezes, the music stops, and it asks:
  - **Continue from checkpoint**: the menu slides away and strips sweep
    across the play area. Behind them everything (score, power, bombs,
    enemies, bullets, boss HP) goes back to the checkpoint and the retry
    count shows, then the strips sweep off, the music carries on from where
    it was at the checkpoint (WAV, OGG and MIDI alike), and you get exactly 2
    seconds of invulnerability.
  - **Give up**: ends the run and shows the result screen.
- **Pause (ESC)** stops the music and opens the Pointdevice pause menu:
  - **Resume** (ESC also resumes): the menu slides away and the music
    carries on
  - **Save and quit**: saves the stage you are on and the score, power,
    bombs and retry count that stage started with, then returns to the
    title screen. Not offered in practice runs.
  - **Quit without saving**
- **Resuming:** there is one saved run per difficulty, character and shot
  type. When you pick Pointdevice Mode for a combination that has one, the
  game shows its stage and score and asks **Resume this run** or **Start a
  new run**. Resuming starts at the beginning of the saved stage and uses the
  save up; starting a new run leaves it in place until you save and quit again.
- **Extends give a bomb** instead of a life.
- **Records stay clean:** Pointdevice runs never save a replay and never write
  `score.dat`.

Menus: Up/Down to move, Z to pick, X to go back from the mode choice. They ignore input for the first third of a
second, so mashing Z through a death never picks an option by accident.

## Where the saved run is stored

- Web build: the browser's `localStorage`, keys
  `th06.pointdevice.<difficulty>.<character>.<shot>`
  (this web build keeps its files in memory only, so a file would not survive
  a reload). Cleared if the player clears site data.
- Desktop: `pointdevice-<difficulty>-<character>-<shot>.sav` in the working
  directory, next to `score.dat`.

## Test helpers (desktop only)

- `TH06_POINTDEVICE=0` or `--legacy` preselects Legacy Mode; `--pointdevice` preselects Pointdevice.
- `TH06_POINTDEVICE_LOG=1` prints checkpoint, death, rewind, save and resume events to stderr.
- `TH06_POINTDEVICE_AUTOTEST=1` starts an autopilot that clicks through the menus,
  plays Stage 1 and picks Continue after every death. `=giveup` gives up at the
  first death; `=savequit` plays on (always invulnerable), saves and quits early
  in Stage 2, then resumes the saved run; `=hold` leaves
  the first menu open so you can look at it.
