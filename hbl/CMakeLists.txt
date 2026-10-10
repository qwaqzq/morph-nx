# Forwarder launcher ("hbl") — the program NSO that a forwarder title runs.
# Built here and embedded into morph as byte arrays (see embed.cmake).
# Source: sphaira hbl/ (nx-hbloader, ISC licence; see nx-hbloader.LICENSE.md).

find_program(NX_ELF2NSO_EXE NAMES elf2nso HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
find_program(NX_NPDMTOOL_EXE NAMES npdmtool HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)

add_executable(hbl_elf
    source/main.c
    source/trampoline.s
)

set_target_properties(hbl_elf PROPERTIES
    OUTPUT_NAME hbl
    C_STANDARD 11
    C_EXTENSIONS ON
)

target_compile_definitions(hbl_elf PRIVATE VERSION="3.0.0")
target_link_options(hbl_elf PRIVATE -Wl,-wrap,exit)

set(HBL_NSO  ${CMAKE_CURRENT_BINARY_DIR}/hbl.nso)
set(HBL_NPDM ${CMAKE_CURRENT_BINARY_DIR}/hbl.npdm)

add_custom_command(
    OUTPUT ${HBL_NSO}
    COMMAND ${NX_ELF2NSO_EXE} $<TARGET_FILE:hbl_elf> ${HBL_NSO}
    DEPENDS $<TARGET_FILE:hbl_elf>
    COMMENT "Converting hbl to NSO"
    VERBATIM
)

add_custom_command(
    OUTPUT ${HBL_NPDM}
    COMMAND ${NX_NPDMTOOL_EXE} ${CMAKE_CURRENT_SOURCE_DIR}/hbl.json ${HBL_NPDM}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/hbl.json
    COMMENT "Generating hbl NPDM"
    VERBATIM
)

# Turn the two binaries into a C++ source that morph links against.
set(HBL_ASSETS_CPP ${CMAKE_CURRENT_BINARY_DIR}/hbl_assets.cpp)
add_custom_command(
    OUTPUT ${HBL_ASSETS_CPP}
    COMMAND ${CMAKE_COMMAND}
        -DIN=${HBL_NSO} -DOUT=${HBL_ASSETS_CPP}.main.inc -DSYM=HBL_MAIN_DATA
        -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    COMMAND ${CMAKE_COMMAND}
        -DIN=${HBL_NPDM} -DOUT=${HBL_ASSETS_CPP}.npdm.inc -DSYM=HBL_NPDM_DATA
        -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    COMMAND ${CMAKE_COMMAND} -E cat
        ${HBL_ASSETS_CPP}.main.inc ${HBL_ASSETS_CPP}.npdm.inc > ${HBL_ASSETS_CPP}
    DEPENDS ${HBL_NSO} ${HBL_NPDM} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Embedding hbl NSO/NPDM"
    VERBATIM
)
add_custom_target(hbl_assets DEPENDS ${HBL_ASSETS_CPP})

add_library(hbl_assets_lib STATIC ${HBL_ASSETS_CPP})
set_target_properties(hbl_assets_lib PROPERTIES LINKER_LANGUAGE CXX)
add_dependencies(hbl_assets_lib hbl_assets)
target_include_directories(hbl_assets_lib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
