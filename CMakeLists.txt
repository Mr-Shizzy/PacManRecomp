cmake_minimum_required(VERSION 3.20)
project(PacManRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/../nesrecomp" CACHE PATH "NESRecomp engine checkout")
include(${NESRECOMP_ROOT}/runner/runner.cmake)

list(APPEND CMAKE_PREFIX_PATH "${NESRECOMP_ROOT}/runner/external/SDL2/cmake")
find_package(SDL2 REQUIRED)

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
    generated/pacman_full.c
    generated/pacman_dispatch.c
)

target_compile_definitions(PacManRecomp PRIVATE
    NESRECOMP_GAME_ID="pacman"
    NESRECOMP_GAME_ROM_CRC32="9e4e9cc2"
    NESRECOMP_GAME_LAUNCHER_PAGES=1
    NESRECOMP_GAME_HIDE_HDPACK_ROW=1)   # graphics come from the Mods page

target_include_directories(PacManRecomp PRIVATE
    ${NESRECOMP_RUNNER_INCLUDE_DIRS}
    ${NESRECOMP_ROOT}/runner/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/generated
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(PacManRecomp PRIVATE ${SDL2_LIBRARIES} ${NESRECOMP_RUNNER_LIBRARIES})

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
