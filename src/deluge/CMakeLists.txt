configure_file(util/firmware_version.h.in util/firmware_version.h @ONLY)
add_subdirectory(dsp)
add_subdirectory(dsp-next)
add_subdirectory(gui/l10n)
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
# The DSP benchmark (bench/) is in the app only on a device build with DELUGE_DSP_BENCH. Its boot hook,
# app_bench.cpp, is always in, and is empty without the option. The host trees build the runner and the cases for
# their spec only (tests/bench); the PMU counters program CP15, which only the device has.
list(FILTER deluge_SOURCES EXCLUDE REGEX .*/bench/.*)
list(APPEND deluge_SOURCES ${CMAKE_CURRENT_LIST_DIR}/bench/app_bench.cpp)
# The streaming benchmark (bench/stream_bench.h) is in the app only on a device build with DELUGE_STREAM_BENCH.
set(_stream_bench_on FALSE)
if(DELUGE_STREAM_BENCH AND NOT DELUGE_HOST)
  set(_stream_bench_on TRUE)
  list(APPEND deluge_SOURCES ${CMAKE_CURRENT_LIST_DIR}/bench/stream_bench.cpp)
endif()
set(_dsp_bench_on FALSE)
if(DELUGE_DSP_BENCH AND NOT DELUGE_HOST)
  set(_dsp_bench_on TRUE)
  list(APPEND deluge_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/bench/runner.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/delay_feedback_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/delay_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/filter_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/compressor_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/mod_fx_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/granular_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/stutter_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/chain_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/pmu_a9.cpp
  )
  # The delay, the filters and the compressor ship from deluge_dsp_fixed, so their cases take that library's flags
  # (dsp-next/CMakeLists.txt): ARM mode, -finline-functions and no LTO, each after the app's own and so in force.
  # The mod FX cases call effects::ModFx::render, which deluge_effects builds; mod_fx_cases.cpp keeps the app's flags.
  set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/bench/delay_feedback_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/delay_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/filter_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/bench/compressor_cases.cpp
    PROPERTIES COMPILE_OPTIONS "-fno-lto;-finline-functions;-marm")
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
add_dependencies(deluge_app deluge_strings deluge_version)

# The UI, input, file and project-edit code, and stem export, are built for size: none runs per sample.
# Excluded: storage's sample streaming, cluster cache and multisample ranges, which voices read while they
# play.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/optimise_for_size.cmake)
set(_deluge_cold_sources ${deluge_SOURCES})
list(FILTER _deluge_cold_sources INCLUDE REGEX
  "/src/deluge/(gui|hid|storage|io/debug|model/(project_bridge|edits|action|consequence|favourite)|processing/stem_export)/")
list(FILTER _deluge_cold_sources EXCLUDE REGEX "/src/deluge/storage/(audio|cluster|multi_range)/")
deluge_optimise_sources_for_size(${_deluge_cold_sources})

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
    deluge_gfx_oled # pure drawing primitives
    deluge_gfx_grid
    deluge_ui_core # the immediate-mode UI toolkit
    deluge_ui_oled
    deluge_ui_grid
    deluge_ui_shell # surfaces, scene stack, input lowering, services, tick, flush
    deluge_notify # the model->UI notification channel
)

target_compile_definitions(deluge_app PUBLIC
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

target_compile_options(deluge_app PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})

if(_dsp_bench_on)
    message(STATUS "DSP benchmarks run at boot (DELUGE_DSP_BENCH)")
    target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH=1)
    if(DELUGE_DSP_BENCH_CASES STREQUAL "lanes")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_LANES=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "chain")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_CHAIN=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "delay")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_DELAY=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "filters")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_FILTERS=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "compressor")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_COMPRESSOR=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "mod_fx")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_MOD_FX=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "granular")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_GRANULAR=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "stutter")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_STUTTER=1)
    elseif(DELUGE_DSP_BENCH_CASES STREQUAL "fx_chains")
        target_compile_definitions(deluge_app PRIVATE DELUGE_DSP_BENCH_FX_CHAINS=1)
    elseif(NOT DELUGE_DSP_BENCH_CASES STREQUAL "kernels")
        message(FATAL_ERROR "DELUGE_DSP_BENCH_CASES is kernels, lanes, chain, delay, filters, compressor, mod_fx, granular, stutter \
or fx_chains, \
not '${DELUGE_DSP_BENCH_CASES}'")
    endif()
    message(STATUS "DSP bench case list: ${DELUGE_DSP_BENCH_CASES}")
endif()

if(_stream_bench_on)
    message(STATUS "Streaming benchmark runs after boot (DELUGE_STREAM_BENCH): ${DELUGE_STREAM_BENCH_SONG_A}, then ${DELUGE_STREAM_BENCH_SONG_B}")
    target_compile_definitions(deluge_app PRIVATE DELUGE_STREAM_BENCH=1
        DELUGE_STREAM_BENCH_SONG_A="${DELUGE_STREAM_BENCH_SONG_A}"
        DELUGE_STREAM_BENCH_SONG_B="${DELUGE_STREAM_BENCH_SONG_B}")
endif()

if(ENABLE_MATRIX_DEBUG)
    message(STATUS "Extra pad matrix debug enabled")
    target_compile_definitions(deluge_app PUBLIC ENABLE_MATRIX_DEBUG=1)
endif(ENABLE_MATRIX_DEBUG)

# NB: deluge_app deliberately references no executable — it is a self-contained,
# platform-agnostic app library that the Rust BSP links, on the device (from
# src/CMakeLists.txt's tree) and on the host (from sim/CMakeLists.txt's).

