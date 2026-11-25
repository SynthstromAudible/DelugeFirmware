cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(DelugeSectionReorderTests CXX)
    enable_testing()
endif()
add_executable(section_reorder_tests
    section_reorder_tests.cpp
    ../../src/deluge/model/consequence/consequence.cpp
    ../../src/deluge/model/consequence/consequence_section_reorder.cpp
)
# Give the lint job explicit native commands for the mock headers too.
set(MOCK_HEADERS
    mocks/definitions_cxx.hpp
    mocks/model/model_stack.h
    mocks/model/song/song.h
    mocks/playback/mode/session.h
)
add_library(section_reorder_mock_headers OBJECT ${MOCK_HEADERS})
set_source_files_properties(${MOCK_HEADERS} PROPERTIES LANGUAGE CXX)
foreach(TARGET section_reorder_tests section_reorder_mock_headers)
    target_compile_features(${TARGET} PRIVATE cxx_std_20)
    target_include_directories(${TARGET} PRIVATE mocks ../../src/deluge)
endforeach()
add_test(NAME SectionReorderUndoRedo COMMAND section_reorder_tests)
