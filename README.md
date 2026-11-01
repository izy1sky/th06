# th06 with Pointdevice Mode

Touhou 6 (Embodiment of Scarlet Devil) for Windows, macOS, Linux and the browser, with a
TH15-style **Pointdevice Mode**: when you get hit you go back to the last checkpoint instead of
losing a life. [POINTDEVICE.md](POINTDEVICE.md) describes how it plays.

This repository has no game data. You need your own copy of the game (version 1.02h).

## Your game folder

Every build reads the game's files from one folder, your *game folder*:

| File | Needed |
|---|---|
| `紅魔郷CM.DAT`, `紅魔郷ED.DAT`, `紅魔郷IN.DAT`, `紅魔郷MD.DAT`, `紅魔郷ST.DAT`, `紅魔郷TL.DAT` | yes, from your copy of the game |
| `msgothic.ttc` | yes. On Windows copy it from `C:\Windows\Fonts` (if it isn't there, add the *Japanese Supplemental Fonts* optional feature); a compatible font such as Kochi Gothic renamed to `msgothic.ttc` also works |
| `bgm\th06_01.wav` … `bgm\th06_17.wav` | for WAV music, from your copy of the game. Without them the desktop builds play MIDI music and the web build has no music |
| `NotoSansJP-Regular.ttf` | optional, used for characters missing from `msgothic.ttc` |

Settings (`東方紅魔郷.cfg`), `score.dat` and Pointdevice saves are written to this folder too.

## Windows

You need Visual Studio 2022 or 2026 with **Desktop development with C++**. In the repository
folder, in PowerShell or cmd:

```bash
powershell -ExecutionPolicy Bypass -File scripts\build_windows.ps1 -Game C:\games\th06
```

Replace `C:\games\th06` with your game folder. The first run downloads premake and SDL2 into
`deps\`. When it finishes, `th06.exe`, `th06_config.exe` and the SDL DLLs are in your game
folder: run `th06.exe` there. `th06_config.exe` changes the settings outside the game.

## macOS

You need the Xcode command line tools (`xcode-select --install`) and [Homebrew](https://brew.sh).

```bash
brew install premake sdl2 sdl2_image sdl2_ttf
premake5 gmake && make -C build config=release
cp th06 ~/games/th06/
```

Then run `./th06` from your game folder (`cd ~/games/th06 && ./th06`).

## Linux

```bash
sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libasound2-dev
premake5 gmake && make -C build config=release
```

Get `premake5` from [premake.github.io](https://premake.github.io/download) (put it on your PATH).
Copy `th06` to your game folder and run it from there. `premake5 gmake --no-asoundlib` builds without MIDI music and without `libasound`.

## Web

The browser version is built with [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
and needs Python 3. If your music is WAV, it is converted to OGG once, which needs `ffmpeg` with
libvorbis (on macOS, `brew install vorbis-tools` also works).

Windows, in the repository folder:

```bash
powershell -ExecutionPolicy Bypass -File scripts\build_web.ps1 -Game C:\games\th06 -Emsdk C:\emsdk
```

```bash
powershell -ExecutionPolicy Bypass -File scripts\serve_web.ps1
```

macOS and Linux:

```bash
source ~/emsdk/emsdk_env.sh
scripts/build_web.sh ~/games/th06
```

```bash
python3 -m http.server -d dist 8000
```

Then open <http://localhost:8000/>. The whole site is in `dist/`; to put it online, upload
everything in that folder. Every file in it is under 20 MB.

**Controls:** arrow keys move, Z shoots, X bombs, Shift moves slowly, Esc pauses, Shift+Enter
switches full screen. Click the game once before using the keyboard. On a touch screen, on-screen
buttons appear: a stick at the bottom left, shoot, bomb and slow at the bottom right, and confirm
and menu at the top right.

## Where this comes from

This repository was forked from [GensokyoClub/th06](https://github.com/GensokyoClub/th06), the
Touhou 6 decompilation, and is built on its
[`portable` branch](https://github.com/GensokyoClub/th06/tree/portable) (EoSD-portable), which
ports the decompiled game to SDL2 and OpenGL. All of the game code, the renderer and the premake
setup come from that project and its contributors. This repository adds the browser build,
Pointdevice Mode, touch controls and the build scripts in `scripts/`. Changes from upstream
`portable` are merged in from time to time.

Like the upstream port, this repository is licensed under the GPLv3 (see [LICENSE](LICENSE)).
