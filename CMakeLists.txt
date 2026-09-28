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
    generated/pacman_full.c
    generated/pacman_dispatch.c
)

target_compile_definitions(PacManRecomp PRIVATE
    NESRECOMP_GAME_ID="pacman"
    NESRECOMP_GAME_ROM_CRC32="9e4e9cc2")

target_include_directories(PacManRecomp PRIVATE
    ${NESRECOMP_RUNNER_INCLUDE_DIRS}
    ${NESRECOMP_ROOT}/runner/src
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/generated
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(PacManRecomp PRIVATE ${SDL2_LIBRARIES} ${NESRECOMP_RUNNER_LIBRARIES})

add_custom_command(TARGET PacManRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${NESRECOMP_ROOT}/runner/external/SDL2/lib/x64/SDL2.dll"
        $<TARGET_FILE_DIR:PacManRecomp>)
