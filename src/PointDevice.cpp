#include "PointDevice.hpp"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "AnmManager.hpp"
#include "AsciiManager.hpp"
#include "BulletManager.hpp"
#include "Controller.hpp"
#include "EclManager.hpp"
#include "EffectManager.hpp"
#include "EnemyManager.hpp"
#include "GameManager.hpp"
#include "Gui.hpp"
#include "ItemManager.hpp"
#include "Player.hpp"
#include "PointDeviceArt.hpp"
#include "Rng.hpp"
#include "SoundPlayer.hpp"
#include "Stage.hpp"
#include "Supervisor.hpp"
#include "ScreenEffect.hpp"
#include "ZunColor.hpp"
#include "utils.hpp"

namespace PointDevice
{

// A periodic checkpoint is wanted after this many gameplay frames (15 seconds).
#define PD_PERIODIC_INTERVAL 900
// A boss attack checkpoint that cannot be taken safely within this many frames is dropped.
#define PD_FORCED_DEADLINE 120
// Invulnerability given after a rewind, so a checkpoint can never become a death loop.
#define PD_RESTORE_INVULNERABILITY 120
#define PD_MESSAGE_FRAMES 90

// Menus ignore input for this many frames after opening, so a player mashing Z
// does not pick an option by accident.
#define PD_MENU_INPUT_DELAY 20

// Leaving a menu. Resume slides the menu out. Continue also sweeps strips across
// the play area, rewinds while it is covered, then sweeps them off again.
#define PD_MENU_EXIT_FRAMES 10
#define PD_SHUTTER_STRIPS 8
#define PD_SHUTTER_STAGGER 2.0f
#define PD_SHUTTER_CLOSE_FRAMES 24
#define PD_SHUTTER_HOLD_FRAMES 24
#define PD_SHUTTER_OPEN_FRAMES 24
#define PD_SHUTTER_COLOR 0xff0e0b16
#define PD_SHUTTER_EDGE_COLOR 0x00e8dcff

// One saved run per difficulty, character and shot type.
#define PD_SAVE_FILE_FORMAT "pointdevice-%d-%d-%d.sav"
#define PD_SAVE_MAGIC 0x56534450 // "PDSV"
#define PD_SAVE_VERSION 1

#define PD_SAFE_BULLET_RADIUS 64.0f
#define PD_SAFE_BULLET_RADIUS_RELAXED 32.0f
#define PD_SAFE_ENEMY_RADIUS 48.0f

// Raw byte copy of a manager. The gameplay managers are plain data: fixed size
// arrays plus pointers into stage data that stays loaded for the whole stage,
// or into other global managers, so a byte copy restores them exactly.
template <typename T> struct Blob
{
    alignas(T) u8 bytes[sizeof(T)];

    void Save(const T *src)
    {
        std::memcpy(this->bytes, (const void *)src, sizeof(T));
    }
    void Load(T *dst) const
    {
        std::memcpy((void *)dst, this->bytes, sizeof(T));
    }
};

struct Snapshot
{
    bool valid;
    Blob<GameManager> gameManager;
    Blob<Player> player;
    Blob<BulletManager> bulletManager;
    Blob<EnemyManager> enemyManager;
    Blob<ItemManager> itemManager;
    Blob<EffectManager> effectManager;
    Blob<Stage> stage;
    Blob<Gui> gui;
    Blob<GuiImpl> guiImpl;
    Blob<EclManager> eclManager;
    Blob<Rng> rng;
    // Stage background VMs live in a heap array, and the stage script toggles
    // flags inside the loaded std file, so both are saved separately.
    std::vector<AnmVm> stageQuadVms;
    std::vector<i8> stageObjectFlags;
    i32 songIdx;
    // Where the music was, so a rewind takes the music back too: the streamed
    // BGM's sample, or the MIDI player's position.
    bool hasBgmPosition;
    u32 bgmPosition;
    bool hasMidiPosition;
    MidiPosition midiPosition;
};

// What a saved run keeps: the selection, the stage, and the stats that stage
// started with. A mid-stage checkpoint cannot be written to disk, because it
// points into stage data that is loaded at a different address next time.
struct SaveData
{
    u32 magic;
    u32 version;
    u8 character;
    u8 shotType;
    u8 difficulty;
    u8 stage;
    u32 score;
    u32 highScore;
    u16 currentPower;
    u16 pointItemsCollected;
    i8 bombsRemaining;
    i8 extraLives;
    i8 powerItemCountForScore;
    u8 padding;
    i32 grazeInTotal;
    i32 rank;
    i32 deaths;
    i32 bombsUsed;
    i32 spellcardsCaptured;
    i32 retries;
};

enum MenuKind
{
    MENU_NONE,
    MENU_DEATH,
    MENU_PAUSE,
};

enum DeathMenuItem
{
    DEATH_MENU_CONTINUE,
    DEATH_MENU_GIVE_UP,
    DEATH_MENU_COUNT,
};

enum PauseMenuItem
{
    PAUSE_MENU_RESUME,
    PAUSE_MENU_SAVE_AND_QUIT,
    PAUSE_MENU_QUIT,
    PAUSE_MENU_COUNT,
};

enum MenuExit
{
    MENU_EXIT_NONE,
    MENU_EXIT_RESUME,
    MENU_EXIT_REWIND,
};

enum ModeSelectStep
{
    MODE_SELECT_CLOSED,
    MODE_SELECT_CHOOSE_MODE,
    MODE_SELECT_ASK_RESUME,
};

enum ModeItem
{
    MODE_ITEM_POINTDEVICE,
    MODE_ITEM_LEGACY,
    MODE_ITEM_COUNT,
};

enum ResumeItem
{
    RESUME_ITEM_RESUME,
    RESUME_ITEM_NEW_RUN,
    RESUME_ITEM_COUNT,
};

enum AutotestMode
{
    AUTOTEST_OFF,
    AUTOTEST_CONTINUE,
    AUTOTEST_GIVE_UP,
    AUTOTEST_SAVE_QUIT,
    AUTOTEST_HOLD_MENU,
};

struct State
{
    bool enabled;
    bool log;
    AutotestMode autotest;
    bool autotestBossSeen;
    bool autotestSaved;
    bool autotestResumed;
    i32 autotestFrames;
    MenuKind menu;
    i32 menuCursor;
    i32 menuFrames;
    // Set once an item is picked: the menu stays up while it animates away.
    MenuExit menuExit;
    i32 menuExitFrames;
    // The music holds still while a menu is open.
    bool musicPaused;
    f32 menuHighlight[PAUSE_MENU_COUNT];
    ModeSelectStep modeSelect;
    i32 modeSelectCursor;
    i32 modeSelectFrames;
    // Frames since the mode choice opened; unlike modeSelectFrames it does not
    // restart when the resume prompt replaces the mode choice.
    i32 modeSelectOpenFrames;
    // 0..1 per item, eased toward the cursor so a selection fades over.
    f32 modeHighlight[MODE_ITEM_COUNT];
    // The saved run for the difficulty, character and shot type being started.
    bool hasSave;
    SaveData save;
    bool resumeChosen;
    bool resuming;
    // The stats the current stage started with, written out by "Save and quit".
    SaveData stageStart;
    bool runActive;
    bool checkpointWanted;
    bool checkpointForced;
    i32 forcedFrames;
    i32 framesSinceCheckpoint;
    i32 songIdx;
    i32 retries;
    i32 checkpointMsgFrames;
    i32 retryMsgFrames;
};

static Snapshot g_Snapshot;
static State g_State;

#define PD_LOG(...)                                                                                                    \
    if (g_State.log)                                                                                                   \
    {                                                                                                                  \
        std::fprintf(stderr, "[pointdevice] frame %u: ", g_GameManager.gameFrames);                                   \
        std::fprintf(stderr, __VA_ARGS__);                                                                             \
        std::fputc('\n', stderr);                                                                                     \
    }

static void PauseMusic(bool paused)
{
    g_State.musicPaused = paused;
    g_SoundPlayer.PauseBgm(paused);
    if (g_Supervisor.midiOutput != NULL)
    {
        if (paused)
        {
            g_Supervisor.midiOutput->Pause();
        }
        else
        {
            g_Supervisor.midiOutput->Resume();
        }
    }
}

#ifdef __EMSCRIPTEN__
// The web build keeps its files in memory only, so saved runs live in
// localStorage (as hex) to survive a page reload.
#define PD_SAVE_STORAGE_KEY_FORMAT "th06.pointdevice.%d.%d.%d"

static bool ReadSaveBytes(const char *name, SaveData *save)
{
    return EM_ASM_INT(
               {
                   var hex = null;
                   try
                   {
                       hex = window.localStorage.getItem(UTF8ToString($0));
                   }
                   catch (e)
                   {
                   }
                   if (!hex || hex.length != $2 * 2)
                       return 0;
                   for (var i = 0; i < $2; i++)
                       HEAPU8[$1 + i] = parseInt(hex.substr(i * 2, 2), 16);
                   return 1;
               },
               name, save, (i32)sizeof(*save)) != 0;
}

static bool WriteSaveBytes(const char *name, const SaveData *save)
{
    return EM_ASM_INT(
               {
                   var hex = "";
                   for (var i = 0; i < $2; i++)
                       hex += (HEAPU8[$1 + i] + 0x100).toString(16).substr(1);
                   try
                   {
                       window.localStorage.setItem(UTF8ToString($0), hex);
                       return 1;
                   }
                   catch (e)
                   {
                       return 0;
                   }
               },
               name, save, (i32)sizeof(*save)) != 0;
}

static void RemoveSaveBytes(const char *name)
{
    EM_ASM(
        {
            try
            {
                window.localStorage.removeItem(UTF8ToString($0));
            }
            catch (e)
            {
            }
        },
        name);
}

static void SaveName(char *name, size_t size, i32 difficulty, i32 character, i32 shotType)
{
    std::snprintf(name, size, PD_SAVE_STORAGE_KEY_FORMAT, difficulty, character, shotType);
}
#else
static bool ReadSaveBytes(const char *name, SaveData *save)
{
    FILE *file;
    bool ok;

    file = std::fopen(name, "rb");
    if (file == NULL)
    {
        return false;
    }
    ok = std::fread(save, sizeof(*save), 1, file) == 1;
    std::fclose(file);
    return ok;
}

static bool WriteSaveBytes(const char *name, const SaveData *save)
{
    FILE *file;
    bool ok;

    file = std::fopen(name, "wb");
    if (file == NULL)
    {
        return false;
    }
    ok = std::fwrite(save, sizeof(*save), 1, file) == 1;
    return std::fclose(file) == 0 && ok;
}

static void RemoveSaveBytes(const char *name)
{
    std::remove(name);
}

static void SaveName(char *name, size_t size, i32 difficulty, i32 character, i32 shotType)
{
    std::snprintf(name, size, PD_SAVE_FILE_FORMAT, difficulty, character, shotType);
}
#endif

// Looks up the saved run for this difficulty, character and shot type.
static void LoadSave(i32 difficulty, i32 character, i32 shotType)
{
    char name[64];
    SaveData save;

    g_State.hasSave = false;
    SaveName(name, sizeof(name), difficulty, character, shotType);
    if (ReadSaveBytes(name, &save) && save.magic == PD_SAVE_MAGIC && save.version == PD_SAVE_VERSION &&
        save.difficulty == difficulty && save.character == character && save.shotType == shotType &&
        save.stage >= 1 && save.stage <= 7)
    {
        g_State.save = save;
        g_State.hasSave = true;
    }
}

static bool WriteSave()
{
    char name[64];
    const SaveData *save = &g_State.stageStart;

    SaveName(name, sizeof(name), save->difficulty, save->character, save->shotType);
    return WriteSaveBytes(name, save);
}

// A saved run is used up when it is resumed.
static void DeleteSave(const SaveData *save)
{
    char name[64];

    SaveName(name, sizeof(name), save->difficulty, save->character, save->shotType);
    RemoveSaveBytes(name);
}

static const char *DifficultyName(i32 difficulty)
{
    static const char *names[] = {"Easy", "Normal", "Hard", "Lunatic", "Extra"};
    return difficulty >= 0 && difficulty < ARRAY_SIZE_SIGNED(names) ? names[difficulty] : "?";
}

void Init(int argc, char *argv[])
{
    const char *env;
    i32 i;

    g_State.enabled = true;
    env = std::getenv("TH06_POINTDEVICE");
    if (env != NULL)
    {
        g_State.enabled = env[0] != '0';
    }
    env = std::getenv("TH06_POINTDEVICE_LOG");
    g_State.log = env != NULL && env[0] != '0';
    env = std::getenv("TH06_POINTDEVICE_AUTOTEST");
    g_State.autotest = AUTOTEST_OFF;
    if (env != NULL && env[0] != '0')
    {
        g_State.autotest = std::strcmp(env, "giveup") == 0     ? AUTOTEST_GIVE_UP
                           : std::strcmp(env, "savequit") == 0 ? AUTOTEST_SAVE_QUIT
                           : std::strcmp(env, "hold") == 0     ? AUTOTEST_HOLD_MENU
                                                               : AUTOTEST_CONTINUE;
    }
    for (i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--pointdevice") == 0)
        {
            g_State.enabled = true;
        }
        else if (std::strcmp(argv[i], "--legacy") == 0)
        {
            g_State.enabled = false;
        }
    }
}

bool IsEnabled()
{
    return g_State.enabled;
}

bool IsRunActive()
{
    return g_State.runActive;
}


// Selection highlight: 1 for the item under the cursor, 0 for the others,
// eased over a few frames so a change of selection cross-fades.
static void SnapHighlights(f32 *highlights, i32 count, i32 cursor)
{
    i32 i;

    for (i = 0; i < count; i++)
    {
        highlights[i] = i == cursor ? 1.0f : 0.0f;
    }
}

static void StepHighlights(f32 *highlights, i32 count, i32 cursor)
{
    i32 i;

    for (i = 0; i < count; i++)
    {
        highlights[i] += ((i == cursor ? 1.0f : 0.0f) - highlights[i]) * 0.3f;
    }
}

void OpenModeSelect()
{
    g_State.modeSelect = MODE_SELECT_CHOOSE_MODE;
    g_State.modeSelectCursor = g_State.enabled ? MODE_ITEM_POINTDEVICE : MODE_ITEM_LEGACY;
    g_State.modeSelectFrames = 0;
    g_State.modeSelectOpenFrames = 0;
    SnapHighlights(g_State.modeHighlight, MODE_ITEM_COUNT, g_State.modeSelectCursor);
    g_State.resumeChosen = false;
    g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT);
}

bool IsModeSelectOpen()
{
    return g_State.modeSelect != MODE_SELECT_CLOSED;
}

static void MoveModeSelectCursor(i32 count)
{
    if (WAS_PRESSED(TH_BUTTON_UP))
    {
        g_State.modeSelectCursor = (g_State.modeSelectCursor + count - 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU);
    }
    if (WAS_PRESSED(TH_BUTTON_DOWN))
    {
        g_State.modeSelectCursor = (g_State.modeSelectCursor + 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU);
    }
}

static const char *CharacterName(i32 character)
{
    return character == 0 ? "Reimu" : "Marisa";
}

ModeSelectResult UpdateModeSelect()
{
    if (g_State.modeSelect == MODE_SELECT_CLOSED)
    {
        return MODE_SELECT_CANCELLED;
    }
    g_State.modeSelectOpenFrames++;
    StepHighlights(g_State.modeHighlight, MODE_ITEM_COUNT, g_State.modeSelectCursor);
    if (g_State.modeSelectFrames++ < PD_MENU_INPUT_DELAY)
    {
        return MODE_SELECT_OPEN;
    }

    if (g_State.modeSelect == MODE_SELECT_CHOOSE_MODE)
    {
        MoveModeSelectCursor(MODE_ITEM_COUNT);
        if (WAS_PRESSED(TH_BUTTON_RETURNMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK);
            g_State.modeSelect = MODE_SELECT_CLOSED;
            return MODE_SELECT_CANCELLED;
        }
        if (WAS_PRESSED(TH_BUTTON_SELECTMENU))
        {
            g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT);
            g_State.enabled = g_State.modeSelectCursor == MODE_ITEM_POINTDEVICE;
            if (g_State.enabled && !g_GameManager.isInPracticeMode)
            {
                LoadSave(g_GameManager.difficulty, g_GameManager.character, g_GameManager.shotType);
                if (g_State.hasSave)
                {
                    g_State.modeSelect = MODE_SELECT_ASK_RESUME;
                    g_State.modeSelectCursor = RESUME_ITEM_RESUME;
                    g_State.modeSelectFrames = 0;
                    SnapHighlights(g_State.modeHighlight, RESUME_ITEM_COUNT, g_State.modeSelectCursor);
                    return MODE_SELECT_OPEN;
                }
            }
            g_State.modeSelect = MODE_SELECT_CLOSED;
            return MODE_SELECT_START;
        }
        return MODE_SELECT_OPEN;
    }

    MoveModeSelectCursor(RESUME_ITEM_COUNT);
    if (WAS_PRESSED(TH_BUTTON_RETURNMENU))
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_BACK);
        g_State.modeSelect = MODE_SELECT_CHOOSE_MODE;
        g_State.modeSelectCursor = MODE_ITEM_POINTDEVICE;
        g_State.modeSelectFrames = 0;
        SnapHighlights(g_State.modeHighlight, MODE_ITEM_COUNT, g_State.modeSelectCursor);
        return MODE_SELECT_OPEN;
    }
    if (WAS_PRESSED(TH_BUTTON_SELECTMENU))
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT);
        g_State.resumeChosen = g_State.modeSelectCursor == RESUME_ITEM_RESUME;
        g_State.modeSelect = MODE_SELECT_CLOSED;
        return MODE_SELECT_START;
    }
    return MODE_SELECT_OPEN;
}

void OnGameLaunch()
{
    const SaveData *save = &g_State.save;

    if (!g_State.resumeChosen)
    {
        return;
    }
    g_State.resumeChosen = false;
    // GameManager::AddedCallback moves on to the next stage.
    g_GameManager.currentStage = save->stage - 1;
    g_GameManager.bombsRemaining = save->bombsRemaining;
    g_State.resuming = true;
    g_State.autotestResumed = g_State.autotest == AUTOTEST_SAVE_QUIT;
    DeleteSave(save);
}

void OnReturnToMenu()
{
    g_State.modeSelect = MODE_SELECT_CLOSED;
    g_State.runActive = false;
    g_State.menu = MENU_NONE;
    g_State.menuExit = MENU_EXIT_NONE;
    if (g_State.musicPaused)
    {
        PauseMusic(false);
    }
    g_Snapshot.valid = false;
}

// GameManager::AddedCallback has just reset the stats for a new game, so the
// saved ones are put back on top.
static void ApplyResume()
{
    const SaveData *save = &g_State.save;

    g_GameManager.guiScore = save->score;
    g_GameManager.score = save->score;
    if (g_GameManager.highScore < save->highScore)
    {
        g_GameManager.highScore = save->highScore;
    }
    g_GameManager.currentPower = save->currentPower;
    g_GameManager.pointItemsCollected = save->pointItemsCollected;
    g_GameManager.extraLives = save->extraLives;
    g_GameManager.powerItemCountForScore = save->powerItemCountForScore;
    g_GameManager.grazeInTotal = save->grazeInTotal;
    g_GameManager.rank = save->rank;
    g_GameManager.deaths = save->deaths;
    g_GameManager.bombsUsed = save->bombsUsed;
    g_GameManager.spellcardsCaptured = save->spellcardsCaptured;
    g_State.retries = save->retries;
    g_Gui.flags.flag0 = 2;
    g_Gui.flags.flag1 = 2;
    g_Gui.flags.flag2 = 2;
    g_Gui.flags.flag3 = 2;
    g_Gui.flags.flag4 = 2;
    PD_LOG("resumed saved run (stage %d, score %u, power %d, bombs %d, retries %d)", g_GameManager.currentStage,
           save->score, save->currentPower, save->bombsRemaining, save->retries);
}

void OnGameStart(bool isNewGame)
{
    if (isNewGame)
    {
        g_State.runActive = g_State.enabled && !g_GameManager.isInReplay && !g_GameManager.demoMode;
        g_State.retries = 0;
        PD_LOG("new game, pointdevice %s", g_State.runActive ? "active" : "inactive");
        if (g_State.resuming && g_State.runActive)
        {
            ApplyResume();
        }
        g_State.resuming = false;
        if (g_State.runActive)
        {
            // Lives mean nothing here: no stars on the side panel and no
            // life bonus at stage clear.
            g_GameManager.livesRemaining = 0;
            g_Gui.flags.flag0 = 2;
        }
    }
    g_State.menu = MENU_NONE;
    g_State.menuExit = MENU_EXIT_NONE;
    if (g_State.musicPaused)
    {
        PauseMusic(false);
    }
    // Snapshots point into stage data, so they never survive a stage change.
    g_Snapshot.valid = false;
    g_State.checkpointWanted = true;
    g_State.checkpointForced = true;
    g_State.forcedFrames = 0;
    g_State.framesSinceCheckpoint = 0;
    g_State.songIdx = 0;
    g_State.checkpointMsgFrames = 0;
    g_State.retryMsgFrames = 0;
}

static f32 DistanceSquared(const ZunVec3 *a, const ZunVec3 *b)
{
    f32 dx = a->x - b->x;
    f32 dy = a->y - b->y;
    return dx * dx + dy * dy;
}

// A checkpoint is only worth taking when the player can survive the next
// moment after being sent back to it.
static bool IsSafeToCheckpoint(bool relaxed)
{
    const ZunVec3 *playerPos = &g_Player.positionCenter;
    f32 bulletRadius = relaxed ? PD_SAFE_BULLET_RADIUS_RELAXED : PD_SAFE_BULLET_RADIUS;
    i32 i;

    if (g_Player.playerState != PLAYER_STATE_ALIVE && g_Player.playerState != PLAYER_STATE_INVULNERABLE)
    {
        return false;
    }
    if (g_Player.bombInfo.isInUse || g_Gui.HasCurrentMsgIdx())
    {
        return false;
    }
    for (i = 0; i < ARRAY_SIZE_SIGNED(g_BulletManager.bullets); i++)
    {
        if (g_BulletManager.bullets[i].state != 0 &&
            DistanceSquared(&g_BulletManager.bullets[i].pos, playerPos) < bulletRadius * bulletRadius)
        {
            return false;
        }
    }
    if (!relaxed)
    {
        for (i = 0; i < ARRAY_SIZE_SIGNED(g_BulletManager.lasers); i++)
        {
            if (g_BulletManager.lasers[i].inUse)
            {
                return false;
            }
        }
    }
    for (i = 0; i < ARRAY_SIZE_SIGNED(g_EnemyManager.enemies); i++)
    {
        if (g_EnemyManager.enemies[i].flags.active &&
            DistanceSquared(&g_EnemyManager.enemies[i].position, playerPos) <
                PD_SAFE_ENEMY_RADIUS * PD_SAFE_ENEMY_RADIUS)
        {
            return false;
        }
    }
    return true;
}

static void RecordStageStart()
{
    SaveData *save = &g_State.stageStart;

    std::memset(save, 0, sizeof(*save));
    save->magic = PD_SAVE_MAGIC;
    save->version = PD_SAVE_VERSION;
    save->character = g_GameManager.character;
    save->shotType = g_GameManager.shotType;
    save->difficulty = g_GameManager.difficulty;
    save->stage = g_GameManager.currentStage;
    // The real score lives in guiScore until the first frame of a stage.
    save->score = g_GameManager.score > g_GameManager.guiScore ? g_GameManager.score : g_GameManager.guiScore;
    save->highScore = g_GameManager.highScore;
    save->currentPower = g_GameManager.currentPower;
    save->pointItemsCollected = g_GameManager.pointItemsCollected;
    save->bombsRemaining = g_GameManager.bombsRemaining;
    save->extraLives = g_GameManager.extraLives;
    save->powerItemCountForScore = g_GameManager.powerItemCountForScore;
    save->grazeInTotal = g_GameManager.grazeInTotal;
    save->rank = g_GameManager.rank;
    save->deaths = g_GameManager.deaths;
    save->bombsUsed = g_GameManager.bombsUsed;
    save->spellcardsCaptured = g_GameManager.spellcardsCaptured;
    save->retries = g_State.retries;
}

static void TakeCheckpoint()
{
    i32 i;

    if (!g_Snapshot.valid)
    {
        RecordStageStart();
    }
    g_Snapshot.gameManager.Save(&g_GameManager);
    g_Snapshot.player.Save(&g_Player);
    g_Snapshot.bulletManager.Save(&g_BulletManager);
    g_Snapshot.enemyManager.Save(&g_EnemyManager);
    g_Snapshot.itemManager.Save(&g_ItemManager);
    g_Snapshot.effectManager.Save(&g_EffectManager);
    g_Snapshot.stage.Save(&g_Stage);
    g_Snapshot.gui.Save(&g_Gui);
    g_Snapshot.guiImpl.Save(g_Gui.impl);
    g_Snapshot.eclManager.Save(&g_EclManager);
    g_Snapshot.rng.Save(&g_Rng);

    g_Snapshot.stageQuadVms.resize(g_Stage.quadCount);
    if (g_Stage.quadCount > 0)
    {
        std::memcpy((void *)g_Snapshot.stageQuadVms.data(), g_Stage.quadVms, sizeof(AnmVm) * g_Stage.quadCount);
    }
    g_Snapshot.stageObjectFlags.resize(g_Stage.objectsCount);
    for (i = 0; i < g_Stage.objectsCount; i++)
    {
        g_Snapshot.stageObjectFlags[i] = g_Stage.objects[i]->flags;
    }
    g_Snapshot.songIdx = g_State.songIdx;
    g_Snapshot.hasBgmPosition = g_SoundPlayer.GetBgmPosition(&g_Snapshot.bgmPosition);
    g_Snapshot.hasMidiPosition = g_Supervisor.cfg.musicMode == MIDI && g_Supervisor.midiOutput != NULL &&
                                 g_Supervisor.midiOutput->GetPosition(&g_Snapshot.midiPosition);
    g_Snapshot.valid = true;

    g_State.checkpointWanted = false;
    g_State.checkpointForced = false;
    g_State.framesSinceCheckpoint = 0;
    g_State.checkpointMsgFrames = PD_MESSAGE_FRAMES;
    PD_LOG("checkpoint taken (stage %d, score %u, power %d, bombs %d)", g_GameManager.currentStage,
           g_GameManager.score, g_GameManager.currentPower, g_GameManager.bombsRemaining);
}

static void RestoreCheckpoint()
{
    GuiImpl *guiImpl = g_Gui.impl;
    i32 i;

    g_Snapshot.gameManager.Load(&g_GameManager);
    g_Snapshot.player.Load(&g_Player);
    g_Snapshot.bulletManager.Load(&g_BulletManager);
    g_Snapshot.enemyManager.Load(&g_EnemyManager);
    g_Snapshot.itemManager.Load(&g_ItemManager);
    g_Snapshot.effectManager.Load(&g_EffectManager);
    g_Snapshot.stage.Load(&g_Stage);
    g_Snapshot.gui.Load(&g_Gui);
    g_Gui.impl = guiImpl;
    g_Snapshot.guiImpl.Load(g_Gui.impl);
    g_Snapshot.eclManager.Load(&g_EclManager);
    g_Snapshot.rng.Load(&g_Rng);

    if (g_Stage.quadCount > 0)
    {
        std::memcpy(g_Stage.quadVms, (const void *)g_Snapshot.stageQuadVms.data(), sizeof(AnmVm) * g_Stage.quadCount);
    }
    for (i = 0; i < g_Stage.objectsCount; i++)
    {
        g_Stage.objects[i]->flags = g_Snapshot.stageObjectFlags[i];
    }

    // The boss theme starts during the pre-boss dialogue, so rewinding to
    // before the boss has to bring the stage theme back.
    if (g_Snapshot.songIdx != g_State.songIdx && g_Snapshot.songIdx >= 0)
    {
        PD_LOG("restoring music track %d", g_Snapshot.songIdx);
        g_Supervisor.PlayAudio(g_Stage.stdData->songPaths[g_Snapshot.songIdx]);
        // Starting a track unpauses it; it stays quiet until the rewind is over.
        if (g_State.musicPaused)
        {
            PauseMusic(true);
        }
    }
    g_State.songIdx = g_Snapshot.songIdx;
    if (g_Snapshot.hasBgmPosition)
    {
        PD_LOG("music back to sample %u", g_Snapshot.bgmPosition);
        g_SoundPlayer.SeekBgm(g_Snapshot.bgmPosition);
    }
    if (g_Snapshot.hasMidiPosition && g_Supervisor.midiOutput != NULL)
    {
        PD_LOG("midi music back to tick %llu + %llu ms, track 1 at byte %u",
               (unsigned long long)g_Snapshot.midiPosition.tickBase,
               (unsigned long long)g_Snapshot.midiPosition.elapsedMS,
               g_Snapshot.midiPosition.tracks.empty() ? 0 : g_Snapshot.midiPosition.tracks[0].cursorOffset);
        g_Supervisor.midiOutput->SetPosition(&g_Snapshot.midiPosition);
    }

    // While alive the timer counts up, so the restored value can be huge; the
    // invulnerability after a rewind is always exactly this long.
    g_Player.playerState = PLAYER_STATE_INVULNERABLE;
    g_Player.invulnerabilityTimer.SetCurrent(PD_RESTORE_INVULNERABILITY);

    // Redraw the whole side panel.
    g_Gui.flags.flag0 = 2;
    g_Gui.flags.flag1 = 2;
    g_Gui.flags.flag2 = 2;
    g_Gui.flags.flag3 = 2;
    g_Gui.flags.flag4 = 2;

    g_State.retries++;
    PD_LOG("rewound to checkpoint, retry %d (score %u, power %d, bombs %d, lives %d)", g_State.retries,
           g_GameManager.score, g_GameManager.currentPower, g_GameManager.bombsRemaining,
           g_GameManager.livesRemaining);
    g_State.retryMsgFrames = PD_MESSAGE_FRAMES;
    g_State.checkpointMsgFrames = 0;
    g_State.checkpointWanted = false;
    g_State.checkpointForced = false;
    g_State.framesSinceCheckpoint = 0;
}

void OnFrameStart()
{
    if (!g_State.runActive)
    {
        return;
    }

    g_State.framesSinceCheckpoint++;
    if (g_State.framesSinceCheckpoint >= PD_PERIODIC_INTERVAL)
    {
        g_State.checkpointWanted = true;
    }

    if (g_State.checkpointForced)
    {
        // The very first checkpoint of a stage is taken unconditionally.
        if (!g_Snapshot.valid || IsSafeToCheckpoint(true))
        {
            TakeCheckpoint();
            return;
        }
        if (++g_State.forcedFrames >= PD_FORCED_DEADLINE)
        {
            g_State.checkpointForced = false;
        }
    }
    if (g_State.checkpointWanted && IsSafeToCheckpoint(false))
    {
        TakeCheckpoint();
    }
}

static void ForceCheckpoint()
{
    g_State.checkpointWanted = true;
    g_State.checkpointForced = true;
    g_State.forcedFrames = 0;
}

void RequestCheckpoint()
{
    if (!g_State.runActive)
    {
        return;
    }
    ForceCheckpoint();
    g_State.autotestBossSeen = true;
    PD_LOG("boss attack started, checkpoint requested");
}

// So that dying after a midboss's last attack doesn't send you back into it.
void OnBossAttackEnded()
{
    if (!g_State.runActive)
    {
        return;
    }
    ForceCheckpoint();
    PD_LOG("boss attack ended, checkpoint requested");
}

// Practice runs cannot be saved, so their pause menu has no "Save and quit".
static i32 MenuItemCount()
{
    if (g_State.menu == MENU_DEATH)
    {
        return DEATH_MENU_COUNT;
    }
    return g_GameManager.isInPracticeMode ? PAUSE_MENU_COUNT - 1 : PAUSE_MENU_COUNT;
}

static i32 PauseItemAt(i32 cursor)
{
    if (g_GameManager.isInPracticeMode && cursor >= PAUSE_MENU_SAVE_AND_QUIT)
    {
        return cursor + 1;
    }
    return cursor;
}

static void OpenMenu(MenuKind kind)
{
    g_State.menu = kind;
    g_State.menuCursor = 0;
    g_State.menuFrames = 0;
    g_State.menuExit = MENU_EXIT_NONE;
    SnapHighlights(g_State.menuHighlight, PAUSE_MENU_COUNT, 0);
    PauseMusic(true);
}

static void LeaveMenu(MenuExit exit)
{
    g_State.menuExit = exit;
    g_State.menuExitFrames = 0;
}

static i32 MenuExitLength()
{
    return g_State.menuExit == MENU_EXIT_REWIND
               ? PD_SHUTTER_CLOSE_FRAMES + PD_SHUTTER_HOLD_FRAMES + PD_SHUTTER_OPEN_FRAMES
               : PD_MENU_EXIT_FRAMES;
}

// The rewind happens once the strips cover the play area, and the music comes
// back as they start to sweep off.
static void StepMenuExit()
{
    const i32 frame = g_State.menuExitFrames++;

    if (g_State.menuExit == MENU_EXIT_REWIND)
    {
        if (frame == PD_SHUTTER_CLOSE_FRAMES)
        {
            RestoreCheckpoint();
        }
        if (frame == PD_SHUTTER_CLOSE_FRAMES + PD_SHUTTER_HOLD_FRAMES)
        {
            PauseMusic(false);
        }
    }
    if (frame + 1 < MenuExitLength())
    {
        return;
    }
    if (g_State.menuExit == MENU_EXIT_RESUME)
    {
        PauseMusic(false);
    }
    g_State.menu = MENU_NONE;
    g_State.menuExit = MENU_EXIT_NONE;
}

bool OnDeathFinished()
{
    if (!g_State.runActive || !g_Snapshot.valid)
    {
        return false;
    }
    OpenMenu(MENU_DEATH);
    PD_LOG("player died, lives %d", g_GameManager.livesRemaining);
    return true;
}

bool OpenPauseMenu()
{
    if (!g_State.runActive)
    {
        return false;
    }
    if (g_State.menu == MENU_NONE)
    {
        OpenMenu(MENU_PAUSE);
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT);
    }
    return true;
}

static void SelectDeathMenuItem(i32 item)
{
    if (item == DEATH_MENU_CONTINUE)
    {
        LeaveMenu(MENU_EXIT_REWIND);
        return;
    }
    g_State.menu = MENU_NONE;
    PauseMusic(false);
    // Giving up ends the run the same way declining a continue does.
    PD_LOG("gave up (score %u)", g_GameManager.score);
    g_GameManager.guiScore = g_GameManager.score;
    g_Supervisor.curState = SUPERVISOR_STATE_RESULTSCREEN_FROMGAME;
}

static void SelectPauseMenuItem(i32 item)
{
    if (item == PAUSE_MENU_SAVE_AND_QUIT)
    {
        if (!WriteSave())
        {
            PD_LOG("could not write the saved run");
            g_SoundPlayer.PlaySoundByIdx(SOUND_BACK);
            return;
        }
        PD_LOG("saved run (stage %d, score %u) and quit", g_State.stageStart.stage, g_State.stageStart.score);
        g_State.autotestSaved = g_State.autotest == AUTOTEST_SAVE_QUIT;
    }
    if (item == PAUSE_MENU_RESUME)
    {
        LeaveMenu(MENU_EXIT_RESUME);
        return;
    }
    g_State.menu = MENU_NONE;
    PauseMusic(false);
    g_Supervisor.curState = SUPERVISOR_STATE_MAINMENU;
}

bool UpdateInGameMenu()
{
    i32 count;

    if (g_State.menu == MENU_NONE)
    {
        return false;
    }
    // Gameplay is frozen, which is not slowdown.
    g_GameManager.isInMenu = 0;
    if (g_State.menuExit != MENU_EXIT_NONE)
    {
        StepMenuExit();
        return true;
    }
    count = MenuItemCount();
    StepHighlights(g_State.menuHighlight, count, g_State.menuCursor);
    if (g_State.menuFrames++ < PD_MENU_INPUT_DELAY)
    {
        return true;
    }
    if (WAS_PRESSED(TH_BUTTON_UP))
    {
        g_State.menuCursor = (g_State.menuCursor + count - 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU);
    }
    if (WAS_PRESSED(TH_BUTTON_DOWN))
    {
        g_State.menuCursor = (g_State.menuCursor + 1) % count;
        g_SoundPlayer.PlaySoundByIdx(SOUND_MOVE_MENU);
    }
    if (g_State.menu == MENU_PAUSE && WAS_PRESSED(TH_BUTTON_MENU))
    {
        SelectPauseMenuItem(PAUSE_MENU_RESUME);
    }
    else if (WAS_PRESSED(TH_BUTTON_SHOOT))
    {
        g_SoundPlayer.PlaySoundByIdx(SOUND_SELECT);
        if (g_State.menu == MENU_DEATH)
        {
            SelectDeathMenuItem(g_State.menuCursor);
        }
        else
        {
            SelectPauseMenuItem(PauseItemAt(g_State.menuCursor));
        }
    }
    return true;
}

void OnMusicChanged(i32 songIdx)
{
    PD_LOG("music changed to track %d", songIdx);
    g_State.songIdx = songIdx;
}

// Presses one menu button at fixed times once the menu accepts input: DOWN
// (when wanted) and then Z.
static u16 AutotestMenuInput(bool moveDown)
{
    if (moveDown && g_State.menuFrames == PD_MENU_INPUT_DELAY + 5)
    {
        return TH_BUTTON_DOWN;
    }
    return g_State.menuFrames == PD_MENU_INPUT_DELAY + 15 ? TH_BUTTON_SHOOT : 0;
}

u16 AutotestInput()
{
    if (g_State.autotest == AUTOTEST_OFF)
    {
        return 0;
    }
    g_State.autotestFrames++;
    if (g_Supervisor.curState == SUPERVISOR_STATE_GAMEMANAGER)
    {
        if (g_State.menu != MENU_NONE && g_State.autotest == AUTOTEST_HOLD_MENU)
        {
            return 0;
        }
        if (g_State.menu == MENU_DEATH)
        {
            return AutotestMenuInput(g_State.autotest == AUTOTEST_GIVE_UP);
        }
        if (g_State.menu == MENU_PAUSE)
        {
            // Only the save test saves; a pause from losing focus just resumes.
            return AutotestMenuInput(g_State.autotest == AUTOTEST_SAVE_QUIT && !g_State.autotestResumed);
        }
        if (g_State.autotest == AUTOTEST_SAVE_QUIT && !g_State.autotestSaved && !g_State.autotestResumed &&
            g_GameManager.currentStage == 2 && g_GameManager.gameFrames == 600)
        {
            return TH_BUTTON_MENU;
        }
        // The autopilot cannot dodge, so it is kept invulnerable until the
        // first boss attack starts (for =savequit, always).
        if ((!g_State.autotestBossSeen && g_Player.playerState == PLAYER_STATE_ALIVE) ||
            (g_State.autotest == AUTOTEST_SAVE_QUIT &&
             (g_Player.playerState == PLAYER_STATE_ALIVE || g_Player.playerState == PLAYER_STATE_INVULNERABLE)))
        {
            g_Player.playerState = PLAYER_STATE_INVULNERABLE;
            g_Player.invulnerabilityTimer.SetCurrent(60);
        }
        // Sway left and right, tapping Z so that dialogue advances too.
        return (g_State.autotestFrames % 10 != 0 ? TH_BUTTON_SHOOT : 0) |
               ((g_State.autotestFrames / 50) % 2 ? TH_BUTTON_LEFT : TH_BUTTON_RIGHT);
    }
    return g_State.autotestFrames % 20 == 0 ? TH_BUTTON_SHOOT : 0;
}

// ---------------------------------------------------------------------------
// Menu art. The menus are drawn from one atlas (PointDeviceArt.hpp) made to
// match the game's own screens: the difficulty cards for the mode choice and
// resume prompt, and the pause menu's look for the in-game menus.

static GfxTextureHandle g_ArtTexture;
static bool g_ArtLoaded;
static bool g_ArtFailed;

static bool LoadArt()
{
    SDL_Surface *png;
    SDL_Surface *rgba;

    if (g_ArtLoaded || g_ArtFailed)
    {
        return g_ArtLoaded;
    }
    png = IMG_Load_RW(SDL_RWFromConstMem(PointDeviceArt::kAtlasPng, sizeof(PointDeviceArt::kAtlasPng)), 1);
    rgba = png != NULL ? SDL_ConvertSurfaceFormat(png, SDL_PIXELFORMAT_ABGR8888, 0) : NULL;
    if (png != NULL)
    {
        SDL_FreeSurface(png);
    }
    if (rgba == NULL)
    {
        PD_LOG("could not load the menu art: %s", IMG_GetError());
        g_ArtFailed = true;
        return false;
    }
    g_AnmManager->FlushVertexBuffer();
    g_ArtTexture = g_GfxBackend->CreateTexture();
    g_AnmManager->SetCurrentTexture(g_ArtTexture);
    g_GfxBackend->SetTextureFilter();
    g_GfxBackend->SetTextureImage(rgba->w, rgba->h, PIXEL_RGBA, PIXEL_UNSIGNED_BYTE, rgba->pixels);
    SDL_FreeSurface(rgba);
    g_ArtLoaded = true;
    return true;
}

static const PointDeviceArt::Sprite *FindArt(const char *name)
{
    i32 i;

    for (i = 0; i < ARRAY_SIZE_SIGNED(PointDeviceArt::kSprites); i++)
    {
        if (std::strcmp(PointDeviceArt::kSprites[i].name, name) == 0)
        {
            return &PointDeviceArt::kSprites[i];
        }
    }
    return NULL;
}

static f32 ArtWidth(const char *name)
{
    const PointDeviceArt::Sprite *sprite = FindArt(name);
    return sprite != NULL ? sprite->contentWidth : 0.0f;
}

// Draws a sprite with its content's top-left corner at (x, y) in 640x480 screen pixels.
static void DrawArt(const char *name, f32 x, f32 y, f32 alpha = 1.0f)
{
    const PointDeviceArt::Sprite *sprite = FindArt(name);
    VertexTex1DiffuseXyzrhw vertices[4];
    f32 left, top, right, bottom;
    f32 u0, v0, u1, v1;
    i32 i;

    if (sprite == NULL || !LoadArt())
    {
        return;
    }
    left = x - sprite->originX;
    top = y - sprite->originY;
    right = left + sprite->width;
    bottom = top + sprite->height;
    u0 = sprite->x / (f32)PointDeviceArt::kAtlasWidth;
    v0 = sprite->y / (f32)PointDeviceArt::kAtlasHeight;
    u1 = (sprite->x + sprite->w) / (f32)PointDeviceArt::kAtlasWidth;
    v1 = (sprite->y + sprite->h) / (f32)PointDeviceArt::kAtlasHeight;
    vertices[0].position = ZunVec4(left, top, 0.0f, 1.0f);
    vertices[1].position = ZunVec4(right, top, 0.0f, 1.0f);
    vertices[2].position = ZunVec4(left, bottom, 0.0f, 1.0f);
    vertices[3].position = ZunVec4(right, bottom, 0.0f, 1.0f);
    vertices[0].textureUV = ZunVec2(u0, v0);
    vertices[1].textureUV = ZunVec2(u1, v0);
    vertices[2].textureUV = ZunVec2(u0, v1);
    vertices[3].textureUV = ZunVec2(u1, v1);
    for (i = 0; i < 4; i++)
    {
        vertices[i].diffuse = ColorData(COLOR_SET_ALPHA(COLOR_WHITE, (u32)(alpha * 255.0f)));
    }

    g_AnmManager->SetCurrentTexture(g_ArtTexture);
    g_AnmManager->SetProjectionMode(PROJECTION_MODE_ORTHOGRAPHIC);
    g_AnmManager->SetVertexAttributes(VERTEX_ATTR_TEX_COORD | VERTEX_ATTR_DIFFUSE);
    g_AnmManager->SetAttributePointer(VERTEX_ARRAY_POSITION, sizeof(*vertices), &vertices[0].position);
    g_AnmManager->SetAttributePointer(VERTEX_ARRAY_TEX_COORD, sizeof(*vertices), &vertices[0].textureUV);
    g_AnmManager->SetAttributePointer(VERTEX_ARRAY_DIFFUSE, sizeof(*vertices), &vertices[0].diffuse);
    g_AnmManager->SetColorOp(COMPONENT_ALPHA, COLOR_OP_MODULATE);
    g_AnmManager->SetColorOp(COMPONENT_RGB, COLOR_OP_MODULATE);
    // With vertex buffers on, the texture is modulated by the texture factor
    // rather than the vertex colour, so set both.
    g_AnmManager->SetTextureFactor(COLOR_SET_ALPHA(COLOR_WHITE, (u32)(alpha * 255.0f)));
    g_AnmManager->SetDepthMask(false);
    g_AnmManager->SetDepthFunc(DEPTH_FUNC_ALWAYS);
    g_GfxBackend->SetBlendMode(BLEND_INV_SRC_ALPHA);
    g_AnmManager->BackendDrawCall();

    // Leave the state the way AnmManager expects, as ScreenEffect::DrawSquare does.
    g_AnmManager->SetCurrentSprite(NULL);
    g_AnmManager->SetCurrentTexture(0);
    g_AnmManager->SetCurrentBlendMode(0xff);
    g_AnmManager->SetDepthFunc(DEPTH_FUNC_LEQUAL);
}

static void DrawArtCentered(const char *name, f32 centerX, f32 y, f32 alpha = 1.0f)
{
    DrawArt(name, centerX - ArtWidth(name) / 2.0f, y, alpha);
}

// Glyph strings: digits, ',', '(', ')' and '#' for 点, drawn from "<prefix><glyph>" sprites.
static void GlyphName(char *name, size_t size, const char *prefix, char glyph)
{
    const char *suffix = glyph == ',' ? "comma" : glyph == '(' ? "lp" : glyph == ')' ? "rp" : glyph == '#' ? "ten" : NULL;
    if (suffix != NULL)
    {
        std::snprintf(name, size, "%s%s", prefix, suffix);
    }
    else
    {
        std::snprintf(name, size, "%s%c", prefix, glyph);
    }
}

static f32 GlyphsWidth(const char *prefix, const char *text)
{
    char name[32];
    f32 width = 0.0f;

    for (; *text != '\0'; text++)
    {
        GlyphName(name, sizeof(name), prefix, *text);
        width += ArtWidth(name);
    }
    return width;
}

static f32 DrawGlyphs(const char *prefix, const char *text, f32 x, f32 y, f32 alpha = 1.0f)
{
    char name[32];

    for (; *text != '\0'; text++)
    {
        GlyphName(name, sizeof(name), prefix, *text);
        DrawArt(name, x, y, alpha);
        x += ArtWidth(name);
    }
    return x;
}

static void FormatScore(char *out, size_t size, u32 score)
{
    char digits[16];
    i32 length;
    i32 i;
    size_t used = 0;

    length = std::snprintf(digits, sizeof(digits), "%u", score);
    for (i = 0; i < length && used + 2 < size; i++)
    {
        if (i > 0 && (length - i) % 3 == 0)
        {
            out[used++] = ',';
        }
        out[used++] = digits[i];
    }
    out[used] = '\0';
}

// 0 at `start` frames, easing out to 1 over `length` frames.
static f32 Ease(i32 frames, f32 start, f32 length)
{
    f32 t = (frames - start) / length;

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

// Cross-fades between the "<base>_off" and "<base>_on" sprites.
static void DrawArtHighlight(const char *base, f32 highlight, f32 x, f32 y, f32 alpha)
{
    char name[32];

    std::snprintf(name, sizeof(name), "%s_off", base);
    DrawArt(name, x, y, alpha * (1.0f - highlight));
    std::snprintf(name, sizeof(name), "%s_on", base);
    DrawArt(name, x, y, alpha * highlight);
}

static void DimRect(f32 left, f32 top, f32 right, f32 bottom, ZunColor color)
{
    ZunRect rect;

    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    ScreenEffect::DrawSquare(&rect, color);
}

// Laid out like the difficulty screen: a heading and two cards. The screen
// dims, the heading drops in, then the cards slide in from the right.
static void DrawModeSelectArt()
{
    const f32 *highlight = g_State.modeHighlight;
    const f32 dim = Ease(g_State.modeSelectOpenFrames, 0.0f, 10.0f);
    const f32 heading = Ease(g_State.modeSelectFrames, 0.0f, 12.0f);
    f32 cards[2];
    char text[32];
    char score[24];
    f32 alpha;
    i32 i;

    for (i = 0; i < 2; i++)
    {
        cards[i] = Ease(g_State.modeSelectFrames, 4.0f + 5.0f * i, 12.0f);
    }
    DimRect(0.0f, 0.0f, 640.0f, 480.0f, COLOR_SET_ALPHA(COLOR_BLACK, (u32)(0x99 * dim)));
    if (g_State.modeSelect == MODE_SELECT_CHOOSE_MODE)
    {
        DrawArtCentered("hdr_rule", 320.0f, 46.0f - 10.0f * (1.0f - heading), heading);
        DrawArtHighlight("card_pd", highlight[MODE_ITEM_POINTDEVICE], 200.0f + 48.0f * (1.0f - cards[0]), 140.0f,
                         cards[0]);
        DrawArtHighlight("card_legacy", highlight[MODE_ITEM_LEGACY], 200.0f + 48.0f * (1.0f - cards[1]), 250.0f,
                         cards[1]);
        return;
    }
    DrawArtCentered("hdr_saved", 320.0f, 46.0f - 10.0f * (1.0f - heading), heading);
    DrawArtHighlight("card_cont", highlight[RESUME_ITEM_RESUME], 200.0f + 48.0f * (1.0f - cards[0]), 140.0f, cards[0]);
    alpha = cards[0] * (0.85f + 0.15f * highlight[RESUME_ITEM_RESUME]);
    std::snprintf(text, sizeof(text), "cont_desc_%d", g_State.save.stage < 7 ? g_State.save.stage : 7);
    DrawArt(text, 208.0f + 48.0f * (1.0f - cards[0]), 176.0f, alpha);
    FormatScore(score, sizeof(score), g_State.save.score);
    std::snprintf(text, sizeof(text), "(%s#)", score);
    DrawGlyphs("card_", text, 430.0f + 48.0f * (1.0f - cards[0]) - GlyphsWidth("card_", text), 196.0f, alpha);
    DrawArtHighlight("card_new", highlight[RESUME_ITEM_NEW_RUN], 200.0f + 48.0f * (1.0f - cards[1]), 250.0f, cards[1]);
}

// Laid out like the game's own pause menu, centred in the play area. The play
// area dims, the title drops in, then the items slide in from the left. Once
// an item is picked they all slide on to the right and fade.
static void DrawInGameMenuArt()
{
    static const char *deathItems[DEATH_MENU_COUNT] = {"opt_retry", "opt_giveup"};
    static const char *pauseItems[PAUSE_MENU_COUNT] = {"opt_resume", "opt_savequit", "opt_quit"};
    const f32 exit =
        g_State.menuExit != MENU_EXIT_NONE ? Ease(g_State.menuExitFrames, 0.0f, PD_MENU_EXIT_FRAMES) : 0.0f;
    const f32 shown = 1.0f - exit;
    const f32 centerX = (GAME_REGION_LEFT + GAME_REGION_RIGHT) / 2.0f + 24.0f * exit;
    const i32 frames = g_State.menuFrames;
    const f32 dim = Ease(frames, 0.0f, 8.0f) * shown;
    const f32 title = Ease(frames, 0.0f, 10.0f) * shown;
    char digits[16];
    char name[32];
    f32 itemsTop;
    f32 x;
    f32 item;
    f32 highlight;
    i32 count;
    i32 i;

    if (shown <= 0.0f)
    {
        return;
    }
    DimRect(GAME_REGION_LEFT, GAME_REGION_TOP, GAME_REGION_RIGHT, GAME_REGION_BOTTOM,
            COLOR_SET_ALPHA(COLOR_BLACK, (u32)(0x80 * dim)));
    count = MenuItemCount();
    if (g_State.menu == MENU_DEATH)
    {
        DrawArtCentered("title_hit", centerX, 160.0f - 8.0f * (1.0f - title), title);
        // "リトライ n回目": this would be retry number n.
        std::snprintf(digits, sizeof(digits), "%d", g_State.retries + 1);
        x = centerX - (ArtWidth("retry_pre") + GlyphsWidth("small_", digits) + ArtWidth("retry_post")) / 2.0f;
        DrawArt("retry_pre", x, 198.0f, title);
        x = DrawGlyphs("small_", digits, x + ArtWidth("retry_pre"), 198.0f, title);
        DrawArt("retry_post", x, 198.0f, title);
        itemsTop = 236.0f;
    }
    else
    {
        DrawArtCentered("title_pause", centerX, 150.0f - 8.0f * (1.0f - title), title);
        itemsTop = 212.0f;
    }
    for (i = 0; i < count; i++)
    {
        const char *base = g_State.menu == MENU_DEATH ? deathItems[i] : pauseItems[PauseItemAt(i)];
        item = Ease(frames, 4.0f + 3.0f * i, 10.0f) * shown;
        highlight = g_State.menuHighlight[i];
        x = centerX - 24.0f * (1.0f - item);
        // The selected style is larger, so it sits a little higher.
        std::snprintf(name, sizeof(name), "%s_off", base);
        DrawArtCentered(name, x, itemsTop + 40.0f * i + 3.0f, item * (1.0f - highlight));
        std::snprintf(name, sizeof(name), "%s_on", base);
        DrawArtCentered(name, x, itemsTop + 40.0f * i, item * highlight);
    }
    if (g_State.menu == MENU_PAUSE && !g_GameManager.isInPracticeMode)
    {
        // The hint fades with the "Save and quit" highlight.
        std::snprintf(name, sizeof(name), "hint_%d", g_State.stageStart.stage < 7 ? g_State.stageStart.stage : 7);
        DrawArtCentered(name, centerX, itemsTop + 40.0f * count + 6.0f,
                        Ease(frames, 10.0f, 8.0f) * shown * g_State.menuHighlight[PAUSE_MENU_SAVE_AND_QUIT]);
    }
}

// The cut out and cut in around a rewind: strips sweep across the play area
// from alternate sides, one after another, until it is covered. The rewind
// happens behind them, "リトライ n回目" shows, then the strips carry on across
// and off the other side.
static void DrawShutter()
{
    const i32 frame = g_State.menuExitFrames;
    const f32 width = GAME_REGION_RIGHT - GAME_REGION_LEFT;
    const f32 height = (GAME_REGION_BOTTOM - GAME_REGION_TOP) / PD_SHUTTER_STRIPS;
    const f32 sweep = PD_SHUTTER_CLOSE_FRAMES - PD_SHUTTER_STAGGER * (PD_SHUTTER_STRIPS - 1);
    const f32 openStart = PD_SHUTTER_CLOSE_FRAMES + PD_SHUTTER_HOLD_FRAMES;
    char digits[16];
    f32 in, out, left, right, top, edge;
    f32 text;
    f32 x;
    i32 i;

    for (i = 0; i < PD_SHUTTER_STRIPS; i++)
    {
        const bool fromLeft = i % 2 == 0;
        const f32 delay = PD_SHUTTER_STAGGER * i;

        in = Ease(frame, delay, sweep);
        out = Ease(frame, openStart + delay, sweep);
        if (in <= 0.0f || out >= 1.0f)
        {
            continue;
        }
        left = fromLeft ? GAME_REGION_LEFT + width * out : GAME_REGION_RIGHT - width * in;
        right = fromLeft ? GAME_REGION_LEFT + width * in : GAME_REGION_RIGHT - width * out;
        top = GAME_REGION_TOP + height * i;
        DimRect(left, top, right, top + height, PD_SHUTTER_COLOR);
        // A bright edge leads each strip in and trails it out.
        if (in < 1.0f || out > 0.0f)
        {
            edge = in < 1.0f ? (fromLeft ? right : left) : (fromLeft ? left : right);
            DimRect(edge - 1.5f, top, edge + 1.5f, top + height,
                    COLOR_SET_ALPHA(PD_SHUTTER_EDGE_COLOR, (u32)(0xe0 * (in < 1.0f ? 1.0f - in : out))));
        }
    }

    text = Ease(frame, PD_SHUTTER_CLOSE_FRAMES, 8.0f) * (1.0f - Ease(frame, openStart, 10.0f));
    if (text <= 0.0f)
    {
        return;
    }
    std::snprintf(digits, sizeof(digits), "%d", g_State.retries);
    x = (GAME_REGION_LEFT + GAME_REGION_RIGHT) / 2.0f -
        (ArtWidth("retry_pre") + GlyphsWidth("small_", digits) + ArtWidth("retry_post")) / 2.0f;
    top = (GAME_REGION_TOP + GAME_REGION_BOTTOM) / 2.0f - 10.0f + 6.0f * (1.0f - text);
    DrawArt("retry_pre", x, top, text);
    x = DrawGlyphs("small_", digits, x + ArtWidth("retry_pre"), top, text);
    DrawArt("retry_post", x, top, text);
}

void DrawMenus()
{
    if (g_State.modeSelect == MODE_SELECT_CLOSED && (g_State.menu == MENU_NONE || !g_State.runActive))
    {
        return;
    }
    // Sprites drawn so far may still be batched; draw them first so the menus cover them.
    g_AnmManager->FlushVertexBuffer();
    // The menus are laid out in whole-screen coordinates, whatever viewport the
    // last drawn element left behind.
    g_Supervisor.viewport.x = 0;
    g_Supervisor.viewport.y = 0;
    g_Supervisor.viewport.width = GAME_WINDOW_WIDTH;
    g_Supervisor.viewport.height = GAME_WINDOW_HEIGHT;
    g_Supervisor.viewport.Set();
    if (g_State.modeSelect != MODE_SELECT_CLOSED)
    {
        DrawModeSelectArt();
    }
    else
    {
        DrawInGameMenuArt();
        if (g_State.menuExit == MENU_EXIT_REWIND)
        {
            DrawShutter();
        }
    }
}

void DrawHud()
{
    ZunVec3 pos;

    if (!g_State.runActive)
    {
        return;
    }

    g_AsciiManager.SetColor(COLOR_LIGHT_RED);
    pos = ZunVec3(432.0f, 250.0f, 0.0f);
    g_AsciiManager.AddFormatText(&pos, "Pointdevice");
    pos = ZunVec3(432.0f, 270.0f, 0.0f);
    g_AsciiManager.AddFormatText(&pos, "Retry   %d", g_State.retries);

    if (g_State.retryMsgFrames > 0)
    {
        g_State.retryMsgFrames--;
        pos = ZunVec3(GAME_REGION_LEFT + 8.0f, GAME_REGION_BOTTOM - 24.0f, 0.0f);
        g_AsciiManager.AddFormatText(&pos, "Back to checkpoint");
    }
    else if (g_State.checkpointMsgFrames > 0)
    {
        g_State.checkpointMsgFrames--;
        g_AsciiManager.SetColor(COLOR_LIGHTCYAN);
        pos = ZunVec3(GAME_REGION_LEFT + 8.0f, GAME_REGION_BOTTOM - 24.0f, 0.0f);
        g_AsciiManager.AddFormatText(&pos, "Checkpoint");
    }
    g_AsciiManager.SetColor(COLOR_WHITE);
}

}; // namespace PointDevice
