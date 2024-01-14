cmake_minimum_required(VERSION 3.24)
project (DelugeTests)
add_compile_options(
    -m32
    -Og
    -g
    -ggdb
)
# CppUTest
include(FetchContent)
FetchContent_Declare(
    CppUTest
    GIT_REPOSITORY https://github.com/cpputest/cpputest.git
    GIT_TAG        master # or use release tag, eg. v3.8
)
# Set this to ON if you want to have the CppUTests in your project as well.
set(TESTS OFF CACHE BOOL "Switch off CppUTest Test build")
add_compile_definitions(
    CPPUTEST_MEM_LEAK_DETECTION_DISABLED
    IN_UNIT_TESTS=1
)

FetchContent_MakeAvailable(CppUTest)
#strchr is seemingly different in x86
add_compile_options(
    -fpermissive
)

# include the non test project source
include_directories(./mocks)
include_directories(../src/deluge)
include_directories(../src/NE10/inc)
include_directories(../src)


file(GLOB_RECURSE deluge_SOURCES
  # Required for memory tests
  ../src/deluge/memory/*
  # Used for prints
  ../src/deluge/gui/l10n/*
  # Used by most other modules
  ../src/deluge/util/*
  # Voice code
  ../src/deluge/dsp/*
  ../src/deluge/io/midi/learned_midi.cpp
  ../src/deluge/model/action/*
  ../src/deluge/model/clip/*
  ../src/deluge/model/consequence/*
  ../src/deluge/model/drum/*
  ../src/deluge/model/global_effectable/*
  ../src/deluge/model/instrument/*
  ../src/deluge/model/mod_controllable/*
  ../src/deluge/model/model_stack.cpp
  ../src/deluge/model/note/*
  ../src/deluge/model/output.cpp
  ../src/deluge/model/sample/*
  ../src/deluge/model/song/*
  ../src/deluge/model/timeline_counter.cpp
  ../src/deluge/model/voice/*
  ../src/deluge/modulation/*
  ../src/deluge/playback/mode/*
  ../src/deluge/playback/playback_handler_core.cpp
  ../src/deluge/processing/live/*
  ../src/deluge/processing/sound/*
  ../src/deluge/processing/source.cpp
  ../src/deluge/storage/audio/audio_file.cpp
  ../src/deluge/storage/audio/audio_file_holder.cpp
  ../src/deluge/storage/audio/audio_file_manager_core.cpp
  ../src/deluge/storage/audio/audio_file_reader.cpp
  ../src/deluge/storage/audio/audio_file_vector.cpp
  ../src/deluge/storage/cluster/*
  ../src/deluge/storage/multi_range/*
  ../src/deluge/storage/wave_table/*
  # Mock implementations
  mocks/*
)

add_executable(RunAllTests RunAllTests.cpp memory_tests.cpp)
target_sources(RunAllTests PUBLIC ${deluge_SOURCES})
target_sources(RunAllTests PUBLIC
  # We don't glob this because we want to mock src/deluge/storage/audio/audio_file_manager
  ../src/deluge/util/functions.cpp
  ../src/deluge/storage/audio/audio_file_vector.cpp
  ../src/deluge/model/instrument/instrument.cpp
  ../src/deluge/model/instrument/melodic_instrument.cpp
)

set_target_properties(RunAllTests
    PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        LINK_FLAGS -m32
)

target_link_libraries(RunAllTests CppUTest CppUTestExt)
