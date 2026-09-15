# CppUTest
include(FetchContent)
FetchContent_Declare(
        CppUTest
        GIT_REPOSITORY https://github.com/cpputest/cpputest.git
        GIT_TAG eccbc2190c672e598c1dd2bf5e4295f5ba27aad1
)
FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl
        GIT_TAG 20.39.4
)

FetchContent_MakeAvailable(etl)

# argon + SIMDe, for the one firmware UI translation unit this binary compiles.
#
# slicer_preview_tests.cpp drives the REAL gui/ui/slicer.cpp rather than a restatement of it, and
# that file reaches <argon.hpp> transitively (model/song/song.h -> dsp/reverb). The block mirrors
# tests/spec_dsp/CMakeLists.txt; keep the pins in sync with lib/CMakeLists.txt and that sibling.
FetchContent_Declare(argon
        GIT_REPOSITORY https://github.com/stellar-aria/argon
        GIT_TAG 5897db725c1cdee452b5f288614796364468f2ea
)
FetchContent_MakeAvailable(argon)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
    set(UNIT_TESTS_NEON_NATIVE ON)
endif ()

if (NOT UNIT_TESTS_NEON_NATIVE)
    FetchContent_Declare(simde
            GIT_REPOSITORY https://github.com/simd-everywhere/simde
            GIT_TAG v0.8.2
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(simde)
    set(UNIT_TESTS_SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})
    # Parent builds this tree at -m32 on plain Linux, where SIMDe's native path drags in AVX512/FMA
    # headers that don't parse in 32-bit mode.
    if (UNIX AND NOT APPLE)
        add_compile_definitions(SIMDE_NO_NATIVE)
    endif ()
endif ()

# Set this to ON if you want to have the CppUTest's internal tests in your
# project as well.
set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")
if (WIN32)
    # Memory leak detection is off by default on Windows, setting
    # it here will cause a massive spam of redefinition warning during
    # build.
    #
    # We use things like strncat, which Windows wants to warn about: silence those.
    add_compile_definitions(
            IN_UNIT_TESTS=1
            _CRT_SECURE_NO_WARNINGS=1
    )
else ()
    add_compile_definitions(
            CPPUTEST_MEM_LEAK_DETECTION_DISABLED
            IN_UNIT_TESTS=1
    )
endif ()

FetchContent_MakeAvailable(CppUTest)

file(GLOB_RECURSE deluge_SOURCES
        # Mock implementations
        mocks/*
        # For LFO
        ../../src/deluge/util/c_string.cpp
        ../../src/deluge/util/lookuptables.cpp
        ../../src/deluge/util/waves.cpp
        ../../src/deluge/modulation/lfo.cpp
        # For value scaling
        ../../src/deluge/gui/menu_item/value_scaling.cpp
        # For scale tests
        ../../src/deluge/model/scale/musical_key.cpp
        ../../src/deluge/model/scale/note_set.cpp
        ../../src/deluge/model/scale/preset_scales.cpp
        ../../src/deluge/model/scale/utils.cpp
        ../../src/deluge/model/scale/preset_scales.cpp
        ../../src/deluge/model/scale/scale_change.cpp
        ../../src/deluge/model/scale/scale_mapper.cpp
        # For clip iterator tests
        ../../src/deluge/model/song/clip_iterators.cpp
        # For sync tests
        ../../src/deluge/model/sync.cpp
        # For chord tests
        ../../src/deluge/gui/ui/keyboard/chords.cpp
        # For clock output scheduler tests
        ../../src/deluge/playback/clock_output_scheduler.cpp
        # For name comparison / default naming tests
        ../../src/deluge/util/name_compare.cpp
        ../../src/deluge/gui/ui/browser/default_name.cpp
        # For waveform peak math tests
        ../../src/deluge/gui/waveform/waveform_peak_math.cpp
        # For the song-preview decode differential: the REAL deserializer stack, not a stand-in.
        # storage/file_reader.cpp exists so this is possible at all -- storage_manager.cpp, where the
        # reader used to live, reaches the whole instrument model to build what it loads.
        # mock_file_io.cpp (borrowed from tests/spec_io) backs deluge::io::File with an in-memory card.
        ../../src/deluge/storage/song_preview.cpp
        ../../src/deluge/storage/file_reader.cpp
        # The REAL weak fallbacks, not a hand-written stand-in: this is exactly what a BSP without
        # the Rust channels resolves, and linking them means the host cannot drift from them.
        ../../src/deluge/storage/file_write_abi.cpp
        ../../src/deluge/storage/file_bytes_abi.cpp
        ../../src/deluge/storage/Deserializer.cpp
        ../../src/deluge/storage/JsonDeserializer.cpp
        ../../src/deluge/io/file.cpp
        ../../src/deluge/util/c_string.cpp
        ../../src/deluge/util/firmware_version.cpp
        ../../src/deluge/util/semver.cpp
        ../spec_io/mock_file_io.cpp
        # For the slicer audition specs: the REAL UI translation unit, so the specs drive the
        # shipped preview()/graphicsRoutine() instead of a restatement of them. Its world is
        # mocks/slicer_env_mock.cpp (globbed above).
        ../../src/deluge/gui/ui/slicer.cpp
        # For the DX cartridge window spec: the REAL sysex parser, so the spec measures what
        # DX7Cartridge::load actually does with a truncated bank rather than restating arithmetic.
        ../../src/deluge/storage/DX7Cartridge.cpp
)

# util/firmware_version.h is generated from a template at firmware-build time, and the deserializers
# the song-preview tests link include it. Configure the REAL template here rather than adding another
# hand-written stub: a stub would drift, and the firmware version this reports is what the parser
# compares a song's earliestCompatibleFirmware against.
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)
configure_file(../../src/deluge/util/firmware_version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/util/firmware_version.h @ONLY)

add_executable(UnitTests
        RunAllTests.cpp
        audio_pacing_tests.cpp
        scheduler_tests.cpp
        lfo_tests.cpp
        scale_tests.cpp
        value_scaling_tests.cpp
        clip_iterator_tests.cpp
        function_tests.cpp
        sync_tests.cpp
        chord_tests.cpp
        time_tests.cpp
        clock_output_scheduler_tests.cpp
        name_compare_tests.cpp
        default_name_tests.cpp
        rgb_tests.cpp
        notes_state_tests.cpp
        waveform_peak_math_tests.cpp
        waveform_overview_cache_tests.cpp
        song_preview_window_tests.cpp
        song_preview_decode_tests.cpp
        song_preview_deferral_tests.cpp
        slicer_preview_tests.cpp
        dx_cartridge_window_tests.cpp
)
# WORKING_DIRECTORY: song_preview_window_tests.cpp reads a checked-in fixture by a path relative to
# the repo root (tests/data/songs/...) and DELUGE_SONG_CORPUS points at a real directory on disk, so
# the binary needs to run from the same place a developer invoking it by hand would, not ctest's
# default (the build tree).
add_test(NAME UnitTests
        COMMAND UnitTests
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../..)
target_sources(UnitTests PRIVATE ${deluge_SOURCES})
target_include_directories(UnitTests PRIVATE
        # include the non test project source
        mocks
        ../../include
        ../../src
        ../../src/deluge
        ../../src/OSLikeStuff        # scheduler_api.h, reached via libdeluge/scheduler.h
        ../spec_io                   # mock_file_io.h, the in-memory card behind deluge::io::File
        ${CMAKE_CURRENT_BINARY_DIR}/generated # util/firmware_version.h, configured just above
)

# The NEON backend gui/ui/slicer.cpp reaches through dsp/reverb.
target_link_libraries(UnitTests argon)
if (NOT UNIT_TESTS_NEON_NATIVE)
    target_include_directories(UnitTests PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/../../sim/compat # <arm_neon.h> shim
            ${UNIT_TESTS_SIMDE_INCLUDE_DIR}            # <simde/arm/neon.h>
            ${UNIT_TESTS_SIMDE_INCLUDE_DIR}/simde      # argon's bare <arm/neon.h>
    )
endif ()

# libstdc++'s parallel STL backend is oneTBB, whose headers pull in the full <immintrin.h> and
# collide with SIMDe's NEON alias macros. The firmware has no TBB; mirror the sim and take the
# serial backend.
target_compile_definitions(UnitTests PRIVATE _GLIBCXX_USE_TBB_PAR_BACKEND=0)

set_target_properties(UnitTests
        PROPERTIES
        C_STANDARD 23
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_link_libraries(UnitTests CppUTestExt etl::etl)

# strchr is seemingly different in x86
target_compile_options(UnitTests PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)

deluge_apply_test_sanitizers(UnitTests)
