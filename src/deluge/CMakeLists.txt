configure_file(util/firmware_version.h.in util/firmware_version.h @ONLY)
add_subdirectory(dsp)
add_subdirectory(gui/l10n)
add_subdirectory(gui/menu_item/generate)
add_subdirectory(version)

# Add the sources to the target
file(GLOB_RECURSE deluge_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.S
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

# Filter out DSP sources as those are built as part of the deluge_dsp library
list(FILTER deluge_SOURCES EXCLUDE REGEX .*/dsp/.*)

# ---------------------------------------------------------------------------
# deluge_app: the PORTABLE application as a static library. It links only the
# portable libraries and the <libdeluge/...> boundary headers — NO HAL (RZA1)
# and NO BSP. The boundary's deluge_* functions are undefined symbols here,
# resolved when a platform executable links its BSP (bsp_rza1 for the firmware,
# the host BSP for the sim, an Embassy shim later). That deluge_app compiles
# with no RZA1/ on its include path is the build-enforced proof that the app has
# no transitive HAL dependency. See sim/CMakeLists.txt for the host consumer.
# ---------------------------------------------------------------------------
# An OBJECT library (not STATIC): its object files embed directly into whichever
# executable links it — exactly as the pre-refactor target_sources(deluge ...) did.
# That keeps the app objects always-present, so the mutual references between the
# app, deluge_dsp (wavetables / allocator) and bsp_rza1 (the boundary) resolve
# without static-archive scan-order games.
add_library(deluge_app OBJECT
  ${deluge_SOURCES}
  # Strings-sources are generated build artefacts, so list them explicitly: a
  # clean nukes them and the source glob would not find them.
  ${deluge_strings_SOURCES}
)
add_dependencies(deluge_app deluge_strings deluge_version deluge_menus)

target_include_directories(deluge_app PUBLIC
    ${deluge_version_INCLUDES}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
    ${SHARED_INCLUDE}
    ${CMAKE_SOURCE_DIR}/include # <libdeluge/...> boundary headers
    ${CMAKE_SOURCE_DIR}/crates/deluge_resource/include # deluge_resource.h (Rust resource manager C ABI)
)

target_link_libraries(deluge_app PUBLIC
    fatfs
    NE10
    eyalroz_printf
    deluge_dsp
    deluge_scheduler # the cooperative task scheduler (provides scheduler_api.h on its path)
    argon
    etl::etl
    deluge_foundation
    deluge_midi
)

target_compile_definitions(deluge_app PUBLIC
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

target_compile_options(deluge_app PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})

if(ENABLE_SYSEX_LOAD)
    message(STATUS "Sysex firmware loading enabled")
    target_compile_definitions(deluge_app PUBLIC ENABLE_SYSEX_LOAD=1)
endif(ENABLE_SYSEX_LOAD)

if(ENABLE_MATRIX_DEBUG)
    message(STATUS "Extra pad matrix debug enabled")
    target_compile_definitions(deluge_app PUBLIC ENABLE_MATRIX_DEBUG=1)
endif(ENABLE_MATRIX_DEBUG)

# NB: deluge_app deliberately does NOT reference the firmware `deluge` executable
# — it is a self-contained, platform-agnostic app library that both the firmware
# (src/CMakeLists.txt) and the host-sim (sim/CMakeLists.txt) assemble against
# their own BSP + entry point.

