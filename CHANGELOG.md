# Changelog

The version number is set only in `CMakeLists.txt` (`project(Lightmere VERSION x.y.z)`). It appears in the window
title and in the properties of the Windows executable.

## 1.0.0

- Graphics filter (options menu, one choice instead of two switches): CRT, DYNAMIC LIGHT or OFF; F5 switches
  the CRT filter, F9 Dynamic Light. The CRT filter (crt-pixie style) is computed at the integer scale of each part
  of the view: the room, the panel corners, the place name, the game menus and the screens outside the game.
  Setting `graphics_filter` (`crt`, `light`, `off`); CRT by default. The view keeps its integer scales.
- Options panel: the same fixed size in every port (256 x 200 pixels of its own, at an integer scale of its own),
  drawn over the picture and never through the CRT filter. Long values and footers (for example the name of a
  controller) are split on more lines instead of widening the panel; only if the text needs more rows the panel
  grows downwards.
  The options menu now uses this panel (the font of the common core) in the four languages.

## 0.9.0

- Windows: the window and the fullscreen picture use the real pixels of the screen also with display scaling
  (125%, 150%, ...): before, Windows stretched the window and the integer scales of the view were lost (blurred,
  smaller picture).
- First start: the window opens at half the desktop size (then the saved size, as before).
- Window title: name, version, game and frames per second.
- CI: Windows built with MSVC + Ninja + vcpkg (`windows-ci` preset, as the other ports); the `windows-vcpkg` preset
  for Visual Studio 2022 stays for local builds.

- **Correcting the game texts**: `lmtext` (developer tool) writes the texts of a language as readable files and
  checks the corrected ones; `--texts <folder>` plays with the corrections. The messages are encoded again in the
  game's format, so they can be longer or shorter than the originals; longer place names are shown in full in
  the Lightmere view.

## 0.8.0 (first public release)

- The project is now called **Lightmere**; the executable is `lightmere`. Licence GPL-3.0-or-later; most of the
  code and documentation was written by an AI assistant (Claude), directed and tested by the author.
- **The game files are read directly from the four ADF disk images**, or from the WHDLoad installation. A new
  loader written from scratch replaces the WHDLoad slave: no WHDLoad, Kickstart or slave file is needed. If the
  files are missing, a message box says where to copy them.
- **Game folder and version check**: `game_path` in `lightmere.ini` (relative to the executable or absolute,
  default: next to the executable). If the files are not there, the system's folder picker asks for the folder,
  and the choice is saved. Every game file is checked against the SHA-256 fingerprints of the known version;
  missing or different files are listed.
- **Options menu** (Esc, or Start on a controller): fullscreen, Dynamic Light, modern background, control ports,
  quit. It is shown in the language chosen in the game (English, French, German, Italian), in English before that.
- **Control ports**: in the options menu, each of the two Amiga control ports can be given nothing, the keyboard
  (numeric keypad 8/4/6/5 and Right Ctrl, so the rest of the keyboard stays the Amiga keyboard), the mouse, or any
  joystick or game controller found by the system, also plugged in while the game runs. The choice is saved in
  `lightmere.ini` (`port1`, `port2`). The arrow keys are now the Amiga cursor keys.
- Dynamic Light is a little brighter, and the Soften Light filter was removed. F9 switches Dynamic Light on and off.
- `--debug`: full window title (level, room, mode, filter, background) and the developer keys F8 (original screen)
  and F10 (teleport). Without it the title shows only the version and the frame rate.
- The shape of the panel is rebuilt at run time from the user's game files instead of being stored in the source.
- Sources and comments translated to English; developer tools are built only with `-DLM_BUILD_TOOLS=ON`.
- Single self-contained Windows executable (static SDL2 and C++ runtime); automated builds for Windows, Linux
  and macOS; portable zip for Windows, Linux build for glibc 2.34 and later (Ubuntu 22.04, Debian 12, ...).

## Before 0.8.0 (private development versions)

- **0.7.5**: Dynamic Light filter; choice of the background behind menus and dialogs (blurred or the original
  black checkerboard), inactive dialog windows blurred too; clean fades entering and leaving interiors; menus
  without stray background pixels; settings kept between runs; frame rate in the title.
- **0.6.1**: debug teleport to any level and room.
- **0.5**: final layout of the new view (place name at the top, panel halves in the corners), panel strings
  rewritten with the game font, menus and dialogs over the blurred layout, smooth room changes, PNG screenshots,
  level presentation pictures without black specks; automated check of every room of the three levels.
- **0.1**: the original game running on the C++ Amiga machine; first version of the room drawn by the C++
  renderer from the game state.
