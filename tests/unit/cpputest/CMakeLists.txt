# CppUTest's UnitTests executable, which only the host tree builds.
deluge_test_directory(TREES host)

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
        # Shared string helper used across several unit tests
        ../../../src/deluge/util/c_string.cpp
        # The deserializer stack whose global readers mock_storage.cpp constructs (mock_storage.cpp also
        # supplies the freezeWithError and deluge::memory::dealloc the slicer sources reach).
        ../../../src/deluge/storage/file_reader.cpp
        # deluge::runtime::nowUs(), the clock behind the stall bounds file_reader.cpp and slice_core.cpp construct.
        ../../../src/deluge/runtime/app_loop.cpp
        # The storage channels' C ABI as "no channel here": the Rust BSP defines them everywhere else.
        ../../support/mocks/storage_channel_mocks.cpp
        ../../../src/deluge/storage/Deserializer.cpp
        ../../../src/deluge/storage/JsonDeserializer.cpp
        ../../../src/deluge/io/file.cpp
        ../../../src/deluge/util/c_string.cpp
        ../../../src/serialization/firmware_version.cpp
        ../../../src/serialization/semver.cpp
        # For the slicer audition specs: the REAL SliceCore translation unit, so the specs drive the
        # shipped audition()/tick_audition() instead of a restatement of them. Its world is
        # tests/support/mocks/mock_slicer_env.cpp.
        ../../../src/deluge/gui/ui/slice_core.cpp
)

# util/firmware_version.h is generated from a template at firmware-build time, and the deserializers
# linked above include it. Configure the REAL template here rather than adding another
# hand-written stub: a stub would drift, and the firmware version this reports is what the parser
# compares a song's earliestCompatibleFirmware against.
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)
configure_file(../../../src/deluge/util/firmware_version.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/generated/util/firmware_version.h @ONLY)

add_executable(UnitTests
        RunAllTests.cpp
        slicer_preview_tests.cpp
)
add_test(NAME UnitTests COMMAND UnitTests)
target_sources(UnitTests PRIVATE ${deluge_SOURCES})
target_include_directories(UnitTests PRIVATE
        # include the non test project source
        ../../../include
        ../../../src
        ../../../src/deluge
        ${CMAKE_CURRENT_BINARY_DIR}/generated # util/firmware_version.h, configured just above
)

# argon's NEON backend: slicer_preview_tests.cpp drives the REAL gui/ui/slice_core.cpp rather than a restatement of it,
# and that file reaches <argon.hpp> transitively (model/song/song.h -> effects/master_chain.h).
deluge_test_neon(UnitTests)

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

# The UI libraries: the legacy canvas and the notify channel the app sources here are built on.
target_link_libraries(UnitTests PRIVATE CppUTestExt etl::etl deluge_gfx_oled deluge_gfx_grid deluge_ui_core deluge_ui_oled
                                        deluge_ui_grid deluge_notify)

# strchr is seemingly different in x86
target_compile_options(UnitTests PUBLIC
        $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>
)

deluge_apply_test_sanitizers(UnitTests)

# The stand-ins these suites link, one copy each in tests/support/mocks.
target_sources(UnitTests PRIVATE
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_display.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_print.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_hal.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_storage.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_firmware_version.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_song.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_slicer_env.cpp
        ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_file_io.cpp
)
target_include_directories(UnitTests PRIVATE ${DELUGE_REPO_ROOT}/tests/support/mocks)
