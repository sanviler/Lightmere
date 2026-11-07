cmake_minimum_required(VERSION 3.20)
# Lightmere version: set only here. It goes into version.h (window title) and the Windows file properties;
# history in CHANGELOG.md.
project(Lightmere VERSION 0.8.0 LANGUAGES CXX)
if(NOT PROJECT_VERSION_PATCH)
  set(PROJECT_VERSION_PATCH 0)
endif()
# always three numbers (x.y.z), as in the window title
set(LM_VERSION_SHORT "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
configure_file(src/version/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)
# Windows: with the vcpkg triplet x64-windows-static (CMakePresets.json) the executable is self-contained,
# SDL2 and the C++ runtime included
if(VCPKG_TARGET_TRIPLET MATCHES "static")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /utf-8)
  # fopen/snprintf are standard and portable: Microsoft's warning C4996 does not apply to this project
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()
option(LM_BUILD_TOOLS "Build the developer tools (amirun, lmtool, lmviewer)" OFF)
# --- game file formats and the room renderer (no external dependencies) ---
add_library(lmformats STATIC
  src/formats/rnc1.cpp
  src/formats/epf.cpp
  src/formats/ilbm.cpp
  src/formats/planar.cpp
  src/formats/text.cpp
  src/formats/palette.cpp
  src/formats/level.cpp
  src/formats/anims.cpp
  src/formats/spawn.cpp
  src/formats/game_files.cpp
  src/platform/sha256.cpp
  src/engine/room_renderer.cpp
)
target_include_directories(lmformats PUBLIC src ${CMAKE_BINARY_DIR}/generated)
# --- 68000 CPU (Moira, MIT licence) and the Amiga machine ---
add_library(moira STATIC ext/moira/Moira.cpp ext/moira/MoiraCore68000.cpp ext/moira/MoiraCore68010.cpp
            ext/moira/MoiraCore68020.cpp ext/moira/MoiraDebugger.cpp)
target_include_directories(moira SYSTEM PUBLIC ext/moira)
if(MSVC)
  target_compile_options(moira PRIVATE /W0 /bigobj)
else()
  target_compile_options(moira PRIVATE -w)
endif()
add_library(lmamiga STATIC
  src/amiga/machine.cpp
  src/amiga/blitter.cpp
  src/amiga/loader.cpp
  src/game/fixes.cpp
  src/game/routines.cpp
  src/game/live_view.cpp
  src/game/panel_text.cpp
  src/game/ham_fix.cpp
)
target_include_directories(lmamiga PUBLIC src)
target_link_libraries(lmamiga PUBLIC moira lmformats)
# --- the game ---
find_package(SDL2 CONFIG REQUIRED)
set(LM_SDL_LIBS $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)
set(LM_GAME_SOURCES
  src/app/main.cpp
  src/app/porting_view.cpp
  src/app/menu_layers.cpp
  src/app/panel_shape.cpp
  src/app/ui.cpp
  src/app/input.cpp
  src/render/enhance.cpp
  src/platform/png_write.cpp
  src/platform/settings.cpp
  src/platform/folder_dialog.cpp
)
if(WIN32)
  configure_file(src/version/lightmere.rc.in ${CMAKE_BINARY_DIR}/generated/lightmere.rc @ONLY)
  list(APPEND LM_GAME_SOURCES ${CMAKE_BINARY_DIR}/generated/lightmere.rc)
endif()

if(APPLE)
  add_executable(lightmere MACOSX_BUNDLE ${LM_GAME_SOURCES})
  set_target_properties(lightmere PROPERTIES
    OUTPUT_NAME "Lightmere"
    MACOSX_BUNDLE_BUNDLE_NAME "Lightmere"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.sanviler.lightmere"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2026 Lorenzo Castelletta"
  )
elseif(WIN32)
  add_executable(lightmere WIN32 ${LM_GAME_SOURCES})
else()
  add_executable(lightmere ${LM_GAME_SOURCES})
endif()

target_link_libraries(lightmere PRIVATE lmamiga ${LM_SDL_LIBS})
if(WIN32)
  target_link_libraries(lightmere PRIVATE ole32 shell32)   # the folder picker (IFileOpenDialog)
endif()
install(TARGETS lightmere
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)
install(FILES README.md LICENSE CHANGELOG.md DESTINATION . OPTIONAL)
# --- developer tools (not shipped) ---
if(LM_BUILD_TOOLS)
  add_executable(amirun src/tools/amirun.cpp)                 # the machine without a window, for tests
  target_link_libraries(amirun PRIVATE lmamiga)
  add_executable(lmtool src/tools/lmtool.cpp)                 # checks of the file formats
  target_link_libraries(lmtool PRIVATE lmformats)
  add_executable(lmviewer src/tools/viewer.cpp src/platform/video.cpp)   # viewer of the graphics and rooms
  target_link_libraries(lmviewer PRIVATE lmformats ${LM_SDL_LIBS})
endif()
