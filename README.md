# th06 with Pointdevice Mode

A playable, portable build of Touhou 6 (Embodiment of Scarlet Devil) for Windows, macOS, Linux and
the browser, with a TH15-style **Pointdevice Mode**: when you get hit you go back to the last
checkpoint instead of losing a life. See [POINTDEVICE.md](POINTDEVICE.md) for how it plays and how
to build it, and [web/README.md](web/README.md) for the browser build.

This repository does not include any game data. You need your own copy of the original game's
`.DAT` files.

## Upstream

This repository was forked from [GensokyoClub/th06](https://github.com/GensokyoClub/th06), the
Touhou 6 decompilation, and is built on its
[`portable` branch](https://github.com/GensokyoClub/th06/tree/portable) (EoSD-portable), which ports
the decompiled game to SDL2 and OpenGL. It is no longer linked to that repository on GitHub and
doesn't send changes back to it. All of the game code, the portable renderer and the build setup
come from that project and its contributors; this repository adds on top of it:

- a browser (WebAssembly) build with OGG music,
- Pointdevice Mode, with its menus, checkpoint saves and music rewind,
- touch controls and Windows build scripts for the web version.

Changes from upstream `portable` are merged in from time to time. Upstream's `master` branch is the
byte-matching decompilation of the original Windows executable and isn't merged here. Like the
upstream port, this repository is licensed under the GPLv3 (see [LICENSE](LICENSE)).

---

The rest of this file is the upstream EoSD-portable readme.

[![Discord][discord-badge]][discord] <- click here to join discord server.

[discord]: https://discord.gg/VyGwAjrh9a
[discord-badge]: https://img.shields.io/discord/1147558514840064030?color=%237289DA&logo=discord&logoColor=%23FFFFFF

This is the readme for the portable fork of EoSD. For the readme of the decomp project, see [here](https://github.com/GensokyoClub/th06/blob/master/README.md).

EoSD-portable is a port of Touhou 6 using SDL2 and OpenGL (with a more general renderer abstraction layer hopefully on the way).
This enables theoretical portability to any system supported by SDL2, with Linux, Windows, and macOS in particular being known to work.
Builds for the BSDs and other Unices are also almost certainly possible, but may require some slight modifications to the build system.

### Platform Requirements

- SDL2, SDL2-image, and SDL2-ttf support
- C++20 standard library support
- A little endian architecture (though big endian support is currently being worked on)
- OpenGL ES 1.1, OpenGL 1.3, or GL 2.1 / GL ES 2.0 / WebGL support

### Dependencies

EoSD-portable has the following dependencies:

- `SDL2`
- `SDL2_image`
- `SDL2_ttf`
- `libasound` (Optional and Linux-only, enables MIDI support. This will almost always be present as part of a desktop distro.)

On Windows and macOS, MIDI support uses the system APIs and needs no extra dependencies.

In addition, building uses [`premake5`](https://premake.github.io/download) and a compiler that supports C++20.

#### Building

In the repository root directory, run `premake5` with the desired build system as an argument (a list can be seen by running `premake5 --help`).
This will output the build files to the `build` directory, and then compilation may be done with the desired build system.

##### Build Options (Use with Premake Invocation)
`--no-asoundlib`: On Linux, doesn't build MIDI support. Removes libasound as a dev and runtime dependency
`--use-c23-embed`: Uses `#embed` for resource inclusion instead of a lua script in the Premake file.

##### Build Example (Debian-based Linux)

Obtain dependencies:

`sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev libasound2-dev`

Generate makefile:

`premake5 gmake`

Compile:

`cd build && make -j16`

##### Build Example (macOS)

Install the Xcode Command Line Tools (provides the compiler), if not already present:

`xcode-select --install`

Obtain dependencies (using [Homebrew](https://brew.sh)):

`brew install premake sdl2 sdl2_image sdl2_ttf`

Generate makefile:

`premake5 gmake`

Compile:

`cd build && make -j16`

### Use

EoSD-portable is designed to be a drop-in replacement for the vanilla EoSD binary.
You will also need to add a font to your game directory with the filename `msgothic.ttc`.
This may be the actual MS Gothic, taken from a Windows machine, or a compatible font such as Kochi Gothic.
EoSD-portable uses the Japanese filenames (e.g. 紅魔郷CM.DAT, 東方紅魔郷.cfg). English and other patches, static or thcrap, do not currently work.
A Japanese locale is not required.

# Decomp Credits

We would like to extend our thanks to the following individuals for their
invaluable contributions:

- @EstexNT for porting the [`var_order` pragma](scripts/pragma_var_order.cpp) to
  MSVC7.
