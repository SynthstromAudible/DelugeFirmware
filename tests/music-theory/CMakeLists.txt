# Specs for src/music-theory/, added by both the x86 (tests/unit) and ARM (tests/unit-arm) test trees.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/music-theory ${CMAKE_CURRENT_BINARY_DIR}/music-theory-lib)
deluge_specs_driver(music_theory_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../support/mocks/mock_panic.cpp
  LIBRARIES deluge_music_theory
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../support/mocks ${CMAKE_CURRENT_LIST_DIR}/../../src)
