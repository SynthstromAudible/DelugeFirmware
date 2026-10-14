# Specs for the Deluge instrument (src/engines/deluge), added by both the x86 (tests/unit) and ARM (tests/unit-arm) test
# trees, built with only the standalone libraries' include roots so a hidden dependency on the app fails this build.
if(NOT TARGET deluge_engine_api)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engine-api ${CMAKE_CURRENT_BINARY_DIR}/engine-api-lib)
endif()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engines/effects ${CMAKE_CURRENT_BINARY_DIR}/effects-lib)
# The sidechain reaches argon through the kernel core; on a host without NEON that needs SIMDe's backend.
deluge_test_neon(deluge_effects)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engines/deluge ${CMAKE_CURRENT_BINARY_DIR}/instrument-lib)
add_subdirectory(deluge)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engines/global_effectable
                 ${CMAKE_CURRENT_BINARY_DIR}/global-effectable-lib)
add_subdirectory(global_effectable)
add_subdirectory(effects)
