configure_file(util/firmware_version.h.in util/firmware_version.h @ONLY)
add_subdirectory(dsp)
add_subdirectory(dsp-next)
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
# Likewise the new dsp-next library (built as deluge_dsp_fixed/_float); its path
# is /dsp-next/, which the /dsp/ regex above does not match.
list(FILTER deluge_SOURCES EXCLUDE REGEX .*/dsp-next/.*)
# The benchmark's Cortex-A9 PMU counters program CP15, which only the device has.
if(DELUGE_HOST)
  list(FILTER deluge_SOURCES EXCLUDE REGEX .*/bench/pmu_a9\\.cpp$)
endif()

# ---------------------------------------------------------------------------
# deluge_app: the PORTABLE application. It links only the portable libraries and
# the <libdeluge/...> boundary headers — no HAL and no BSP. The boundary's
# deluge_* functions are undefined symbols here, resolved when the Rust BSP links
# the app (src/bsp/rust/build/, on the device and on the host).
# scripts/check_platform_boundary.py keeps HAL and BSP includes out of the app.
# ---------------------------------------------------------------------------
# An OBJECT library (not STATIC): its object files embed directly into whichever
# executable links it. That keeps the app objects always-present, so the mutual
# references between the app, deluge_dsp (wavetables / allocator) and the BSP (the
# boundary) resolve without static-archive scan-order games.
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
    NE10
    eyalroz_printf
    deluge_dsp
    deluge_dsp_next # the clean L0 library primitives migrate into (synth untangling Phase 1)
    deluge_scheduler # task_scheduler/clock_type.h on its path
    argon
    etl::etl
    deluge_foundation
    deluge_midi
    deluge_project   # the project model (deluge::project::Song)
    deluge_engine_types # the host–engine contract's data-only types
    deluge_engine_api # the host–engine contract's interface
    deluge_instrument # the Deluge instrument's state, words and codec
    deluge_effects # the effects' words, state and codec
    deluge_global_effectable # the provisional shared-effect param table
    deluge_serialization # the document tree and its XML and JSON parsers
    deluge_music_theory # note sets, scales, keys and scale mapping
)

target_compile_definitions(deluge_app PUBLIC
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

target_compile_options(deluge_app PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})

if(ENABLE_MATRIX_DEBUG)
    message(STATUS "Extra pad matrix debug enabled")
    target_compile_definitions(deluge_app PUBLIC ENABLE_MATRIX_DEBUG=1)
endif(ENABLE_MATRIX_DEBUG)

# NB: deluge_app deliberately references no executable — it is a self-contained,
# platform-agnostic app library that the Rust BSP links, on the device (from
# src/CMakeLists.txt's tree) and on the host (from sim/CMakeLists.txt's).

