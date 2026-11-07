# Lightmere

**Lightmere** is a free, open-source port of the Amiga action-adventure **Darkmere** (Core Design, 1994) to modern
systems. It runs the original game, unchanged, on a small Amiga machine written in C++, and draws it again with a
modern renderer: sharp integer scaling, a widescreen layout, optional dynamic lighting and a cleaner presentation
of menus and dialogs.

> **No part of the original game is included in this project.** Lightmere contains no game code, graphics,
> sound, text, ROM or disk image. To play you need your own copy of Darkmere (see [Game files](#game-files)).
> Darkmere and all its contents are the property of their respective owners. The author of Lightmere claims
> no rights over the game. Lightmere is not affiliated with or endorsed by Core Design or any rights holder.

---

> **Made largely with AI.** Most of Lightmere (the analysis of the original game, the code and the
> documentation) was written by an AI assistant (Anthropic's Claude), directed, tested and reviewed by the
> author.

---

## Features

- **The original game, as it was.** The original 68000 code runs on an emulated Amiga 500 (CPU, copper, blitter,
  bitplanes, sprites, Paula audio, CIAs, keyboard, joystick and mouse ports). No Kickstart ROM is needed.
- **A new view of the game.** The room is redrawn every frame by a C++ renderer from the state of the game:
  - integer scaling with square pixels (6× at 1920×1080, 8× at 2560×1440, 12× in 4K);
  - widescreen layout: the place name in a band at the top, the room below, and the two halves of the original
    panel in the bottom corners (hero on the left, target on the right);
  - torches and lanterns of the neighbouring rooms are animated too;
  - smooth room changes and fades, without the flicker of the original screen.
- **Dynamic Light** (optional): rooms with fire are put in a cool half-light, with a warm, flickering light around
  torches, lanterns and fireplaces. Rooms without fire stay as they are.
- **Modern background for menus and dialogs** (optional): the scene behind menus is blurred and darkened instead
  of covered by the original black checkerboard; inactive dialog windows are blurred too.
- Small fixes of the original: black specks in the level presentation pictures, a typo in the Italian start
  screen, a stray sprite in the loading screens.
- Both Amiga control ports configurable: keyboard, mouse or any joystick or game controller; fullscreen,
  lossless screenshots.
- Two of the game's slowest routines are translated to C++ (the first entries into rooms are faster).

## Game files

Lightmere needs the original Darkmere files: **the four ADF disk images** of Darkmere (any file names,
extension `.adf`), or the **WHDLoad installation** of Darkmere (its `data` folder or the folder that contains it).

By default Lightmere looks for them **in the folder of the executable**: copy the four ADF files next to
`lightmere.exe` and start it. To keep them somewhere else, set the folder in `lightmere.ini` (next to the
executable), relative to the executable or absolute:

```
game_path=.                      # default: next to the executable
game_path=disks                  # a folder next to the executable
game_path=D:/Games/Amiga/Darkmere
```

If the files are not there, Lightmere says so and opens the system's folder picker, so you can choose the folder
with the disk images; the choice is saved in `game_path`. (On Linux the picker needs `zenity` or `kdialog`;
without them, set `game_path` by hand.)

The files are identified by their contents, not by their names: every game file inside the disk images is
checked against the SHA-256 fingerprints of the known version of Darkmere (`src/formats/game_manifest.h`; only
fingerprints, no game data). The whole disk images are not compared, because two good copies of the same disks
can differ in their labels or free space. Both the four-disk set `Darkmere (1994)(Core)(M4)` (including cracked
sets) and the WHDLoad installation are recognised. If a file is missing or different, Lightmere lists which.

The disk images are only read; nothing is written to them. Saved games are kept in the `saves` folder next to
the executable.

## Controls

| Key | Action |
|---|---|
| Numeric keypad **8 4 6 5** (or 2), 7 9 1 3 diagonals | joystick, when the keyboard is assigned to a control port |
| **Right Ctrl** | fire (keypad 0: second button) |
| **Esc** | options menu (fullscreen, dynamic light, modern background, control ports, quit) |
| **F7** | menu and dialog background: modern (blurred) / original (black checkerboard) |
| **F9** | Dynamic Light on / off |
| **F11** | window / fullscreen |
| **F12** | screenshot (PNG next to the executable) |
| Alt+F4 | quit |

All the other keys, arrows and Left Ctrl / Alt included, go to the game as the keys of the Amiga keyboard (for
example `1`–`4` on the language screen).

### Control ports

The options menu sets what is plugged into each of the two Amiga control ports (left / right to change it):
nothing, the **keyboard** (the keypad joystick above), the **mouse**, or any **joystick or game controller** found
by the system (Joystick 1, 2, ..., connected or plugged in at any time; the name of the device is shown at the
bottom of the menu). By default port 1 has the mouse and port 2 the first joystick, or the keyboard if there is
none. Darkmere is played with the joystick in port 2.

Game controller: stick or d-pad = joystick, **A** = fire, **B** = second button. **Start** opens the options menu
from any controller. Joysticks without a controller mapping use their first hat or axes and buttons 1 and 2.

The options are saved in `lightmere.ini` next to the executable.

### Command line

| Option | Meaning |
|---|---|
| `--fullscreen` | start in fullscreen |
| `--data <folder>` | use the game files in this folder (instead of `game_path`) |
| `--saves <folder>` | keep the saved games in this folder |
| `--debug` | developer mode: full window title (level, room, mode, filter), **F8** switches between the Lightmere view and the original screen, **F10** opens a panel to teleport to any level and room |
| `--original` | run only the original game code (no routines translated to C++) |
| `--custom1 N` | cheats for testing: 1 infinite help, 2 infinite magic, 4 infinite energy (add them up) |
| `--texts <path>` | corrected game texts (a folder of text files made with `lmtext`, or one file): see [Correcting the texts](#correcting-the-texts) |

## Building

Lightmere is written in C++20 with CMake and SDL2. The Moira 68000 core is included in `ext/moira`.

### Windows (Visual Studio 2022 + vcpkg)

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with *Desktop development with C++*, and
   [vcpkg](https://github.com/microsoft/vcpkg). Set the environment variable `VCPKG_ROOT` to the vcpkg folder.
2. From a *Developer PowerShell for VS 2022*, in the source folder:
   ```
   cmake --preset windows-vcpkg
   cmake --build --preset release
   ```
   vcpkg downloads and builds SDL2 the first time. The result is `build\Release\lightmere.exe`, a single
   executable with SDL2 included.
3. Optional: `./scripts/package.ps1` makes the portable zip in `dist\`.

### Linux

The ready-made Linux build (`lightmere-<version>-linux-x64.tar.gz`) runs on x86-64 distributions with glibc 2.34
or later (Ubuntu 22.04, Debian 12, Fedora 35 and newer) and needs only SDL2, which most desktops already have
(`sudo apt install libsdl2-2.0-0` on Debian and Ubuntu, `sudo dnf install SDL2` on Fedora). Extract it, copy
the four ADF images next to `lightmere` and run `./lightmere`.

To build it:

```
sudo apt install build-essential cmake libsdl2-dev     # Debian, Ubuntu; other distributions: SDL2 development package
cmake --preset linux
cmake --build build -j
./build/lightmere
```

`-DLM_LINUX_PORTABLE=ON` (used for the release) links the C++ runtime into the executable and avoids the
newest glibc functions, so that it runs on older distributions too.

### macOS (not tested yet)

```
brew install cmake sdl2
cmake --preset linux
cmake --build build -j
```

### Developer tools

`-DLM_BUILD_TOOLS=ON` also builds `amirun` (the machine without a window, for automated tests), `lmtool`
(checks of the file formats), `lmtext` (the game texts as readable files, see below) and `lmviewer` (viewer of
the graphics and rooms). They are not needed to play.

### Correcting the texts

The dialogue texts of Darkmere (about 4,900 messages per language) are kept in the game files in a compressed
form. `lmtext` writes them as plain, readable UTF-8 files, one per level plus the story and the program texts,
with the place where each message is used and the English text as reference; identical texts are written once:

```
lmtext export <game folder> <out folder> [language]     # 0 EN, 1 FR, 2 DE, 3 IT (default)
lmtext check  <game folder> <edited folder>             # characters, lengths, numbers
lmtext diff   <game folder> <edited folder> <out file>  # only the corrected messages
lightmere --texts <edited folder>                       # play with the corrected texts
```

Lightmere puts the corrected messages back into the texts in memory (the files of the game are not changed):
it encodes them again in the game's format and moves the index of the messages, so the texts can be longer or
shorter than the originals. The exported files hold the whole original texts, so they are for the user's own
use and are not part of this project; the corrections shipped with Lightmere will hold only the corrected
messages, each with a check of the original it replaces.

### Automated builds

GitHub Actions builds Windows, Linux and macOS at every push (`.github/workflows/build.yml`). For a version tag
(`v0.8.0`, ...) the portable zips are attached to the GitHub release.

## How it works

```
src/amiga    the Amiga machine: CPU adapter (Moira), custom chips, CIAs, audio, and the game loader, which reads
             the original files from the ADF images or the WHDLoad folder and replaces the game's disk routines
src/formats  the game's file formats: RNC packing, EPF archives, IFF pictures, levels, rooms, texts, animations
src/engine   the room renderer, rebuilt from the original drawing routines
src/game     the live view (the room drawn from the game state in memory), panel texts, fixes, C++ routines
src/app      the Lightmere program: view layout, menus and dialogs over the layout, options menu, main loop
src/render   the Dynamic Light filter
```

The game's own code keeps running: game logic, characters, dialogs and sound are the original ones. Lightmere
reads the state of the game from the emulated memory (current room, draw list, doors, objects, panel strings) and
draws the room itself. Where the C++ view cannot show something yet (menus, dialogs, banners, fades), the original
screen is shown inside the new layout.

## Roadmap

- Localisation revisioning: corrections of the original texts (the tools are ready, the corrections in progress).
- High-quality resampled and remastered audio, switchable at run time.
- More routines translated to C++ (the first entries into level 3 rooms are slow in the original).
- A wider view of the neighbouring rooms.
- **Android build** (contributions welcome): an SDL2 Android project (Gradle, NDK), touch controls (virtual
  joystick, fire, options menu and the game keys), choosing the four ADF images with the system file picker and
  copying them into the app's storage, pause and resume, and a signed APK.

## Credits

- **Darkmere** © 1994 Core Design. Code by Paul A. Hodgson and Andrew J. Buchanan, graphics by Mark K. Jones.
- **Moira** 68000 core by Dirk W. Hoffmann, MIT licence (`ext/moira/LICENSE`).
- **SDL2**, zlib licence.
- Lightmere by Lorenzo Castelletta.

Most of the code and documentation of Lightmere was written by an AI assistant (Anthropic's Claude) under the
direction of the author, who tested and reviewed it.

## Licence

Copyright (C) 2026 Lorenzo Castelletta.

Lightmere is free software, released under the **GNU General Public License v3.0 or later** (see `LICENSE`).
In short: you may use, study, share and modify it; whoever distributes Lightmere or a modified version must
credit the original author, keep the same licence and make the source code available.

The licence covers only the code of Lightmere. It does not cover Darkmere or any of its contents, which are not
part of this project.
