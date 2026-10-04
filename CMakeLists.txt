cmake_minimum_required(VERSION 3.20)
project(PacManRecomp VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/../nesrecomp" CACHE PATH "NESRecomp engine checkout")
include(${NESRECOMP_ROOT}/runner/runner.cmake)

list(APPEND CMAKE_PREFIX_PATH "${NESRECOMP_ROOT}/runner/external/SDL2/cmake")
find_package(SDL2 REQUIRED)

# The updater's script (src/update.ps1), built into the exe as a byte array.
set(UPDATE_PS1 ${CMAKE_SOURCE_DIR}/src/update.ps1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${UPDATE_PS1})
file(READ ${UPDATE_PS1} UPDATE_PS1_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," UPDATE_PS1_BYTES "${UPDATE_PS1_HEX}")
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/update_ps1.c CONTENT
    "const unsigned char g_update_ps1[] = {${UPDATE_PS1_BYTES}0};\nconst unsigned g_update_ps1_len = sizeof(g_update_ps1) - 1;\n")

add_executable(PacManRecomp
    ${NESRECOMP_RUNNER_SOURCES}
    src/extras.c
    src/pause_menu.c
    src/options.c
    src/nes_text.c
    src/highscores.c
    src/soundpack.c
    src/logo.c
    src/mods.c
    src/hdbuild.c
    src/modgen.c
    src/sounddump.c
    src/launcher_pages.c
    src/controls_page.c
    src/capture.c
    src/updater.c
    ${CMAKE_BINARY_DIR}/update_ps1.c
)

# PACMAN_GAME_DLL: build the exe without the game code, which then comes from
# game.dll (src/gamedll/game_dll.c, compiled with TinyCC on the player's PC).
option(PACMAN_GAME_DLL "Load the game code from game.dll" OFF)
if(PACMAN_GAME_DLL)
    target_sources(PacManRecomp PRIVATE src/game_dll_host.c)
    target_compile_definitions(PacManRecomp PRIVATE
        PACMAN_GAME_DLL=1 PACMAN_VERSION_BARE=${PROJECT_VERSION})
    target_include_directories(PacManRecomp PRIVATE ${CMAKE_SOURCE_DIR}/src/gamedll)
else()
    target_sources(PacManRecomp PRIVATE generated/pacman_full.c generated/pacman_dispatch.c)
    target_include_directories(PacManRecomp PRIVATE ${CMAKE_SOURCE_DIR}/generated)
endif()

target_compile_definitions(PacManRecomp PRIVATE
    NESRECOMP_GAME_ID="pacman"
    NESRECOMP_GAME_ROM_CRC32="9e4e9cc2"
    NESRECOMP_GAME_LAUNCHER_PAGES=1
    PACMAN_VERSION="${PROJECT_VERSION}"
    NESRECOMP_GAME_HIDE_HDPACK_ROW=1)   # graphics come from the Mods page

target_include_directories(PacManRecomp PRIVATE
    ${NESRECOMP_RUNNER_INCLUDE_DIRS}
    ${NESRECOMP_ROOT}/runner/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(PacManRecomp PRIVATE ${SDL2_LIBRARIES} ${NESRECOMP_RUNNER_LIBRARIES})
if(WIN32)
    target_link_libraries(PacManRecomp PRIVATE winhttp shell32 advapi32)   # updater.c
endif()

# Bake the compiler's C++ runtime into the exe, so players only need SDL2.dll
# (llvm-mingw otherwise wants libc++.dll and libunwind.dll next to the game).
# -mwindows: a normal Windows app, so no black console window opens beside it.
if(MINGW)
    target_link_options(PacManRecomp PRIVATE
        -static-libstdc++ -static-libgcc -Wl,-Bstatic -lunwind -Wl,-Bdynamic
        -mwindows)
endif()

# ---- Pre-boot GUI launcher (recomp-ui / Dear ImGui) ----
# Defines RECOMP_LAUNCHER on the target, un-gating the launcher block in
# launcher.c's main(), and stages the launcher assets next to the exe.
set(RECOMP_UI_ROOT "${CMAKE_SOURCE_DIR}/../recomp-ui" CACHE PATH "recomp-ui checkout")
include(${RECOMP_UI_ROOT}/recomp_ui.cmake)
recomp_target_launcher_ui(PacManRecomp CONSOLE nes)

add_custom_command(TARGET PacManRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NESRECOMP_ROOT}/runner/external/SDL2/lib/x64/SDL2.dll"
        $<TARGET_FILE_DIR:PacManRecomp>)
