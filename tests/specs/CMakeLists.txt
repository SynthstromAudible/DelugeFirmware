# tests/specs: CppUTest-driven specs that assert a property over checked-in fixture DATA (e.g. the
# song corpus, tests/data/songs/) rather than exercising a mocked seam. Kept separate from
# tests/unit's single monolithic UnitTests target so a data-driven spec can be dropped in here by
# just adding a *_spec.cpp file, without editing a shared source list.
include(FetchContent)
FetchContent_Declare(
        CppUTest
        GIT_REPOSITORY https://github.com/cpputest/cpputest.git
        GIT_TAG eccbc2190c672e598c1dd2bf5e4295f5ba27aad1
)
FetchContent_MakeAvailable(CppUTest)

add_compile_definitions(
        CPPUTEST_MEM_LEAK_DETECTION_DISABLED
        IN_UNIT_TESTS=1
)

file(GLOB spec_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*_spec.cpp)

add_executable(DataSpecTests
        RunAllSpecs.cpp
        ${spec_sources}
)
target_include_directories(DataSpecTests PRIVATE
        ../../src/deluge
)
target_link_libraries(DataSpecTests PRIVATE CppUTestExt)
set_target_properties(DataSpecTests PROPERTIES
        CXX_STANDARD 26
        CXX_STANDARD_REQUIRED ON
)

# WORKING_DIRECTORY: the repo root, not the build tree -- these specs read checked-in fixtures by a
# path relative to the repo (tests/data/songs/...) and DELUGE_SONG_CORPUS points at a real directory
# on disk, so the binary needs to run from the same place a developer invoking it by hand would.
add_test(NAME DataSpecTests COMMAND DataSpecTests WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../..)
