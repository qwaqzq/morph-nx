cmake_minimum_required(VERSION 3.13)

include($ENV{DEVKITPRO}/cmake/Switch.cmake REQUIRED)

project(morph VERSION 0.1.0 LANGUAGES C CXX ASM)

# Fetch NanoVG
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/ITotalJustice/nanovg-deko3d.git
    GIT_TAG 845c9fc
)

FetchContent_Declare(stb_nanovg
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 5c20573
)

set(NANOVG_NO_JPEG OFF)
set(NANOVG_NO_PNG OFF)
set(NANOVG_NO_BMP OFF)
set(NANOVG_NO_PSD ON)
set(NANOVG_NO_TGA ON)
set(NANOVG_NO_GIF ON)
set(NANOVG_NO_HDR ON)
set(NANOVG_NO_PIC ON)
set(NANOVG_NO_PNM ON)

if (CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(NANOVG_DEBUG ON)
endif()

FetchContent_Declare(minIni
    GIT_REPOSITORY https://github.com/ITotalJustice/minIni-nx.git
    GIT_TAG 6e952b6
)

FetchContent_MakeAvailable(nanovg stb_nanovg minIni)

# Forwarder launcher (hbl): built to NSO/NPDM and embedded into morph
add_subdirectory(hbl)

# Main executable
add_executable(morph
    source/main.cpp
    source/app.cpp
    source/ui/renderer.cpp
    source/ui/particles.cpp
    source/nro.cpp
    source/fs.cpp
    source/image.cpp
    source/log.cpp
    source/i18n.cpp
    source/settings.cpp
    source/shortcuts.cpp
    source/keys.cpp
    source/forwarder/build.cpp
    source/forwarder/icon.cpp
    source/forwarder/install.cpp
)

target_compile_definitions(morph PRIVATE
    -DAPP_VERSION="${PROJECT_VERSION}"
    -Dmorph_USE_LOG=1
)

target_compile_options(morph PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare
    -Os -flto -ffunction-sections -fdata-sections
)

target_link_options(morph PRIVATE
    -flto -Wl,--gc-sections
)

set_target_properties(morph PROPERTIES
    C_STANDARD 23
    CXX_STANDARD 26
    CXX_EXTENSIONS ON
)

target_include_directories(morph PRIVATE
    include
)

target_compile_options(morph PRIVATE
    --embed-dir=${CMAKE_SOURCE_DIR}/assets
)

# STB
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${stb_nanovg_SOURCE_DIR})

target_link_libraries(morph PRIVATE
    nanovg
    stb
    minIni
    hbl_assets_lib
)

# NACP
nx_generate_nacp(
    OUTPUT morph.nacp
    NAME "Morph"
    AUTHOR "qwaqzq"
    VERSION ${PROJECT_VERSION}
)

# RomFS
file(COPY ${CMAKE_SOURCE_DIR}/assets/romfs DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
dkp_add_asset_target(morph_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

# Shaders
dkp_install_assets(morph_romfs
    DESTINATION shaders
    TARGETS
        fill_aa_fsh
        fill_fsh
        fill_vsh
)

# NRO
nx_create_nro(morph
    OUTPUT ${CMAKE_BINARY_DIR}/morph.nro
    ICON ${CMAKE_SOURCE_DIR}/assets/icon.jpg
    NACP morph.nacp
    ROMFS morph_romfs
)

message(STATUS "Morph v${PROJECT_VERSION}")
