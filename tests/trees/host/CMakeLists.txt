# Gate unit:
#     every test directory under tests/unit that names the host tree (tests/support/cmake/specs.cmake): the CppSpec
#     drivers and the model-equality driver on the committed songs; the DSP library specs and the synth library's link
#     closure (tests/unit/dsp); and the gfx-layer and layer-rules checks, on the -m32 host: policies,
#     parsers, storage state machines, the UI's pure steps, DSP kernels against scalar references.
#     Pass extra ctest arguments after --, e.g. `dbt test unit -- -R stitch_spec`

cmake_minimum_required(VERSION 3.24)
project(DelugeTests)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/ccache.cmake)

# MAINTAINER_MODE, DELUGE_C_STANDARD / DELUGE_CXX_STANDARD and DELUGE_COMMON_COMPILE_OPTIONS,
# also included by the device (top-level CMakeLists.txt) and host-sim (src/platform/host/CMakeLists.txt)
# projects. The UI libraries libs.cmake adds (src/ui's and src/base/notify) apply
# DELUGE_COMMON_COMPILE_OPTIONS themselves, so their warning set is identical wherever they build.
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deluge_common_options.cmake)

enable_testing()

find_package(Python3 COMPONENTS Interpreter)
if (Python3_Interpreter_FOUND)
    add_test(NAME layers
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../../scripts/check_layers.py)
    add_test(NAME layer_rules
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../../scripts/check_layer_rules.py)
endif ()

# ETL (etl::string) is used by util/string helpers pulled in transitively via functions.h,
# so make it available to the spec drivers.
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deps.cmake)
FetchContent_MakeAvailable(etl)
link_libraries(etl::etl)

add_compile_options(-Wno-unknown-warning-option)
#needs to support -m32 since the allocator conformance assumes a 32 bit architecture
if (UNIX AND NOT APPLE)
    # -mfpmath=sse -msse2: do double math in SSE (64-bit IEEE) rather than the x87 stack (80-bit). x87 excess precision
    # makes exact float comparisons depend on when intermediates spill to memory, which sanitizer instrumentation
    # perturbs (it caused a spurious failure in the Time double-conversion test). SSE has no excess precision and so
    # also matches the ARM target's VFP behaviour, making host float results a better proxy for the firmware's.
    add_compile_options(-m32 -mfpmath=sse -msse2 -Og -ggdb3)
    add_link_options(-m32)
endif ()

set(DELUGE_TEST_TREE host)
include(${CMAKE_CURRENT_LIST_DIR}/../../support/cmake/specs.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../support/cmake/libs.cmake)

# The allocator, resource and sample crates, members of the root Cargo workspace.
set(RUST_ALLOC ${CMAKE_CURRENT_LIST_DIR}/../../../src/base/alloc)
set(RUST_RESOURCE ${CMAKE_CURRENT_LIST_DIR}/../../../src/storage/resource)
set(RUST_SAMPLE_FILL ${CMAKE_CURRENT_LIST_DIR}/../../../src/storage/sample/fill)
set(RUST_SAMPLE_READER ${CMAKE_CURRENT_LIST_DIR}/../../../src/storage/sample/reader)
set(RUST_SAMPLE_PEAKS ${CMAKE_CURRENT_LIST_DIR}/../../../src/storage/sample/peaks)
set(RUST_SAMPLE_SOURCE ${CMAKE_CURRENT_LIST_DIR}/../../../src/storage/sample/source)
set(RUST_STREAM ${CMAKE_CURRENT_LIST_DIR}/../../../src/storage/stream)
set(RUST_CONVERT ${CMAKE_CURRENT_LIST_DIR}/../../../src/storage/sample/convert)
set(RUST_TEST_RUST ${CMAKE_CURRENT_LIST_DIR}/rust)

if (UNIX AND NOT APPLE)
    # Build the deluge_test_rust umbrella staticlib (i686) ONCE and share it across the
    # conformance specs (it re-exports both the allocator and resource-manager C
    # ABIs). Defining the build here avoids two subdirs emitting colliding rules for
    # the same output.
    # Built from the root workspace with its host-test profile (opt 3, LTO, panic=abort: the C++ binaries abort on
    # panic), into this tree's own target dir.
    set(RUST_WS ${CMAKE_CURRENT_LIST_DIR}/../../..)
    set(RUST_TARGET i686-unknown-linux-gnu)
    set(RUST_LIB ${RUST_WS}/target/tests-host/${RUST_TARGET}/host-test/libdeluge_test_rust.a)
    file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS
        ${RUST_ALLOC}/src/*.rs ${RUST_RESOURCE}/src/*.rs ${RUST_TEST_RUST}/src/*.rs)
    add_custom_command(
        OUTPUT ${RUST_LIB}
        # The target dir is named, as RUST_LIB is: `dbt test` sets CARGO_TARGET_DIR for its own cargo builds, and a
        # build that followed it would leave RUST_LIB as it was, so the specs would link a stale decoder.
        COMMAND cargo build -p deluge_test_rust --profile host-test --target ${RUST_TARGET}
            --target-dir ${RUST_WS}/target/tests-host
        WORKING_DIRECTORY ${RUST_WS}
        DEPENDS ${RUST_SOURCES}
            ${RUST_WS}/Cargo.lock
            ${RUST_WS}/Cargo.toml ${RUST_ALLOC}/Cargo.toml
            ${RUST_RESOURCE}/Cargo.toml ${RUST_TEST_RUST}/Cargo.toml
        COMMENT "Building Rust deluge_test_rust staticlib (${RUST_TARGET})"
        VERBATIM)
    add_custom_target(deluge_rust_rs_build DEPENDS ${RUST_LIB})
    add_library(deluge_rust_rs STATIC IMPORTED GLOBAL)
    set_target_properties(deluge_rust_rs PROPERTIES IMPORTED_LOCATION ${RUST_LIB})
    add_dependencies(deluge_rust_rs deluge_rust_rs_build)

    # A second umbrella build, with `sample_abi` on, for the sample_reader spec: the sample
    # crates export their C ABI on a host build only with their `c_abi` feature, which
    # `sample_abi` turns on. Its own --target-dir keeps the two feature sets' outputs apart.
    set(RUST_LIB_SIM ${RUST_WS}/target/tests-host-sim/${RUST_TARGET}/host-test/libdeluge_test_rust.a)
    # Own glob (not layered on RUST_SOURCES -- GLOB_RECURSE patterns are wildcards, not a list of
    # already-resolved paths to re-glob), mirroring src/platform/host/CMakeLists.txt's own SIM_RUST_SOURCES.
    file(GLOB_RECURSE RUST_SOURCES_SIM CONFIGURE_DEPENDS
        ${RUST_ALLOC}/src/*.rs ${RUST_RESOURCE}/src/*.rs
        ${RUST_SAMPLE_READER}/src/*.rs ${RUST_SAMPLE_PEAKS}/src/*.rs ${RUST_SAMPLE_FILL}/src/*.rs
        ${RUST_SAMPLE_SOURCE}/src/*.rs ${RUST_STREAM}/src/*.rs ${RUST_CONVERT}/src/*.rs ${RUST_CONVERT}/build.rs
        ${RUST_TEST_RUST}/src/*.rs)
    add_custom_command(
        OUTPUT ${RUST_LIB_SIM}
        COMMAND cargo build -p deluge_test_rust --profile host-test --target ${RUST_TARGET} --features sample_abi
            --target-dir ${RUST_WS}/target/tests-host-sim
        WORKING_DIRECTORY ${RUST_WS}
        DEPENDS ${RUST_SOURCES_SIM}
            ${RUST_WS}/Cargo.lock
            ${RUST_WS}/Cargo.toml ${RUST_ALLOC}/Cargo.toml
            ${RUST_RESOURCE}/Cargo.toml ${RUST_TEST_RUST}/Cargo.toml
            ${RUST_SAMPLE_READER}/Cargo.toml ${RUST_SAMPLE_PEAKS}/Cargo.toml ${RUST_SAMPLE_FILL}/Cargo.toml
            ${RUST_SAMPLE_SOURCE}/Cargo.toml ${RUST_STREAM}/Cargo.toml ${RUST_CONVERT}/Cargo.toml
        COMMENT "Building Rust deluge_test_rust staticlib with the sim feature (${RUST_TARGET})"
        VERBATIM)
    add_custom_target(deluge_rust_rs_sim_build DEPENDS ${RUST_LIB_SIM})
    add_library(deluge_rust_rs_sim STATIC IMPORTED GLOBAL)
    set_target_properties(deluge_rust_rs_sim PROPERTIES IMPORTED_LOCATION ${RUST_LIB_SIM})
    add_dependencies(deluge_rust_rs_sim deluge_rust_rs_sim_build)
elseif (APPLE)
    # macOS gets the umbrella staticlib too, because deluge_spec needs it: audio_file_format.cpp's
    # header parse IS the Rust decoder (deluge-sdk's deluge-audio-format). Built for the DEFAULT host
    # target rather than i686 -- the specs are 64-bit native here (no -m32 above), and letting cargo
    # pick the host beats guessing an apple-darwin triple. The alloc/resource conformance specs stay
    # Linux-only; this branch defines nothing but the shared archive.
    set(RUST_WS ${CMAKE_CURRENT_LIST_DIR}/../../..)
    set(RUST_LIB ${RUST_WS}/target/tests-host/host-test/libdeluge_test_rust.a)
    file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS
        ${RUST_ALLOC}/src/*.rs ${RUST_RESOURCE}/src/*.rs ${RUST_TEST_RUST}/src/*.rs)
    add_custom_command(
        OUTPUT ${RUST_LIB}
        # The target dir is named for the reason the Linux branch gives: `dbt test` sets CARGO_TARGET_DIR, and a
        # build that followed it would leave RUST_LIB stale.
        COMMAND cargo build -p deluge_test_rust --profile host-test --target-dir ${RUST_WS}/target/tests-host
        WORKING_DIRECTORY ${RUST_WS}
        DEPENDS ${RUST_SOURCES}
            ${RUST_WS}/Cargo.lock
            ${RUST_WS}/Cargo.toml ${RUST_ALLOC}/Cargo.toml
            ${RUST_RESOURCE}/Cargo.toml ${RUST_TEST_RUST}/Cargo.toml
        COMMENT "Building Rust deluge_test_rust staticlib (host)"
        VERBATIM)
    add_custom_target(deluge_rust_rs_build DEPENDS ${RUST_LIB})
    add_library(deluge_rust_rs STATIC IMPORTED GLOBAL)
    set_target_properties(deluge_rust_rs PROPERTIES IMPORTED_LOCATION ${RUST_LIB})
    add_dependencies(deluge_rust_rs deluge_rust_rs_build)
else ()
    # The app's specs link the umbrella staticlib (audio_file_format.cpp's header parse is its Rust decoder), and only
    # the two branches above build it.
    message(FATAL_ERROR "the host tree needs the deluge_test_rust staticlib (deluge_rust_rs), "
                        "built here for Linux and macOS only")
endif ()

# deluge_spec: the app code the host specs exercise outside the app's own libraries, built once and linked by every
# directory whose specs test the app or the UI (app_specs, gfx_specs, ui_widgets_specs, shell_specs, notify_specs,
# scenes_specs, midi_specs, base_specs, harness_specs, io_debug_specs).
set(_src ${DELUGE_REPO_ROOT}/src)
set(_tests ${DELUGE_REPO_ROOT}/tests)
add_library(deluge_spec STATIC
  # The audio-file header parser and its dependency-free byte and format helpers (the file-loading regression net).
  ${_src}/deluge/storage/audio/audio_file_format.cpp
  ${_src}/deluge/storage/build_value.cpp
  ${_src}/deluge/storage/value_tree_deserializer.cpp
  ${_src}/deluge/util/audio_format_helpers.cpp
  ${_src}/deluge/util/string.cpp
  # intToString and floatToString, from the dependency-free base library.
  ${_src}/base/string_format.c
  ${_src}/deluge/util/c_string.cpp
  ${_src}/deluge/util/lookuptables/lookuptables.cpp
  ${_tests}/support/mocks/mock_display.cpp
  ${_tests}/support/mocks/mock_display_c_abi.cpp
  ${_tests}/support/mocks/mock_print.cpp
  ${_tests}/support/mocks/mock_firmware_version.cpp
  # ObjectPool guards its free list with CriticalSectionGuard, whose C ABI the BSP owns; the specs have no BSP, and
  # single-threaded drivers have nothing to mask.
  ${_tests}/support/mocks/mock_critical_section.cpp
  # The pad-grid crash pattern, against fault_pattern_spec.cpp's recording board.
  ${_src}/deluge/io/debug/fault_pattern.c
  # The UI layers' code under test that is not in their own libraries: the guest's surfaces and host UI against fakes
  # of the legacy symbols they touch, the hid button decode, the menu node ids and validator (model-free) against fake
  # shortcuts and bindings, and the level-0 and level-3 harness writers and tables.
  ${_src}/deluge/hid/display/oled_canvas/canvas.cpp
  ${_src}/deluge/gui/ui/notify_drain_mapping.cpp
  ${_src}/deluge/gui/ui/shell_guest_surfaces.cpp
  ${_tests}/unit/deluge/gui/support/fake_legacy_hid.cpp
  ${_src}/deluge/gui/ui/shell_host_ui.cpp
  ${_tests}/unit/deluge/gui/support/fake_device_registry.cpp
  ${_tests}/unit/deluge/gui/support/fake_legacy_ui.cpp
  ${_src}/deluge/hid/button.cpp
  ${_src}/ui/scenes/menu/node_id.cpp
  ${_src}/ui/scenes/menu/validate.cpp
  ${_tests}/unit/ui/scenes/menu/support/fake_shortcuts.cpp
  ${_tests}/unit/ui/scenes/menu/support/fake_bindings_relevant.cpp
  ${_tests}/harness/ui_script.cpp
  ${_tests}/harness/snapshot.cpp
  ${_tests}/harness/level3/host_script_tables.c
  ${_tests}/harness/level3/host_snapshot.c
)
# deluge_ui_widgets_oled: the starfield and rainfall animations the specs and previews drive. deluge_rust_rs: the
# umbrella staticlib above, because audio_file_format.cpp's header parse is its Rust decoder (deluge-sdk's
# deluge-audio-format, reached through include/libdeluge/audio_format.h).
target_link_libraries(deluge_spec PUBLIC eyalroz_printf deluge_midi deluge_serialization deluge_ui_widgets_oled
  deluge_ui_gfx_oled deluge_ui_gfx_grid deluge_ui_widgets_core deluge_ui_widgets_grid deluge_ui_shell deluge_base_notify
  deluge_rust_rs)
target_include_directories(deluge_spec PUBLIC ${_src}/deluge ${_src} ${_tests}/support/mocks
                                                  ${DELUGE_REPO_ROOT}/include)
set_target_properties(deluge_spec PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES C_STANDARD 23
                                             C_STANDARD_REQUIRED YES)
# The trees the UI specs read and write besides the goldens (GOLDEN_ROOT, which every spec driver gets from
# tests/support/cmake/golden.cmake): the level-2 scripts, and the scratch the runner writes its snapshots into before
# diffing them.
target_compile_definitions(deluge_spec PUBLIC
  DELUGE_REPO_ROOT="${DELUGE_REPO_ROOT}"
  UI_SCRIPTS_DIR="${_tests}/ui-scripts/shell"
  UI_GOT_DIR="${CMAKE_CURRENT_BINARY_DIR}/got"
)
# tests/ for "harness/..." and "support/golden_update.h".
target_include_directories(deluge_spec PRIVATE ${_tests})

# Every test directory under tests/unit that names this tree (tests/support/cmake/specs.cmake).
deluge_spec_directories(${DELUGE_REPO_ROOT}/tests/unit)
