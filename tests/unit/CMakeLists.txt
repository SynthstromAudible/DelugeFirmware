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
        ../../src/deluge/storage/Deserializer.cpp
        ../../src/deluge/storage/JsonDeserializer.cpp
        ../../src/deluge/io/file.cpp
        ../../src/deluge/util/c_string.cpp
        ../../src/deluge/util/firmware_version.cpp
        ../../src/deluge/util/semver.cpp
        ../spec_io/mock_file_io.cpp
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
