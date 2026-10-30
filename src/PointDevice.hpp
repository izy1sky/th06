#pragma once

#include "inttypes.hpp"

// Pointdevice Mode, modelled on the mode of the same name in Touhou 15.
//
// Instead of losing a life, a death rewinds the game to the last checkpoint.
// A checkpoint is an in-memory snapshot of every gameplay manager (player,
// enemies, bullets, items, effects, stage background, GUI, ECL, RNG, score...).
// Checkpoints are taken at the start of each stage, at the start of every boss
// attack, and periodically during the stage whenever the player is not in
// immediate danger.
//
// After each death a menu asks whether to continue from the checkpoint or give
// up. In a Pointdevice run, ESC opens a pause menu that can save and quit: the
// saved run (current stage and the stats the stage started with) is offered
// again the next time the same difficulty, character and shot type are picked.
//
// Pointdevice runs never save replays (a rewind breaks replay sync) and never
// write score.dat, so legacy records stay untouched.
namespace PointDevice
{
// Reads --pointdevice / --legacy and the TH06_POINTDEVICE=0/1 environment variable.
void Init(int argc, char *argv[]);
bool IsEnabled();

// Mode choice, shown after the shot type is picked: Pointdevice or Legacy,
// then (for Pointdevice) whether to resume the saved run for this difficulty,
// character and shot type.
enum ModeSelectResult
{
    MODE_SELECT_OPEN,
    MODE_SELECT_CANCELLED,
    MODE_SELECT_START,
};
void OpenModeSelect();
bool IsModeSelectOpen();
ModeSelectResult UpdateModeSelect();
// Called right before the main menu starts the game; applies a chosen resume.
void OnGameLaunch();
void OnReturnToMenu();

// Called from GameManager::AddedCallback for every stage.
void OnGameStart(bool isNewGame);
bool IsRunActive();

// ESC was pressed in-game. Returns true when the Pointdevice pause menu handles it.
bool OpenPauseMenu();
// Runs the death and pause menus. Returns true while one is open, in which
// case gameplay is frozen for this frame.
bool UpdateInGameMenu();

// Called once per unpaused gameplay frame, before any other gameplay manager updates.
void OnFrameStart();

// A boss attack (spell card or non-spell) just started.
void RequestCheckpoint();

// Called when the death animation is over. Returns true if the death menu was
// opened, in which case the caller must not take a life away.
bool OnDeathFinished();

void OnMusicChanged(i32 songIdx);
void DrawHud();
// From AsciiManager::OnDrawMenus, after everything else: the mode choice and
// the death and pause menus.
void DrawMenus();

// Test autopilot (TH06_POINTDEVICE_AUTOTEST=1): taps Z through the menus, then
// sways left and right while shooting in-game, and continues after each death.
// =giveup gives up at the first death instead. =savequit never dies, saves and
// quits early in Stage 2, then resumes the saved run.
// =hold leaves the first menu open, for screenshots.
u16 AutotestInput();
}; // namespace PointDevice
