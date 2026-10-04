# Specs for the Deluge instrument (src/engines/deluge), added by both the x86 (tests/unit) and ARM (tests/unit-arm) test
# trees, built with only the standalone libraries' include roots so a hidden dependency on the app fails this build.
if(NOT TARGET deluge_engine_api)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engine-api ${CMAKE_CURRENT_BINARY_DIR}/engine-api-lib)
endif()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engines/deluge ${CMAKE_CURRENT_BINARY_DIR}/instrument-lib)
add_subdirectory(deluge)
