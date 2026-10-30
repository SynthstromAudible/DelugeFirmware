# Specs for the Deluge instrument (src/instruments/deluge), added by both the x86 (tests/unit) and ARM (tests/unit-arm)
# test trees after tests/effects (the instrument links the effects), built with only the standalone libraries' include
# roots so a hidden dependency on the app fails this build.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/instruments/deluge ${CMAKE_CURRENT_BINARY_DIR}/instrument-lib)
add_subdirectory(deluge)
# The global-effectable table's specs check it against the instrument's table, so they build here.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/effects/global_effectable
                 ${CMAKE_CURRENT_BINARY_DIR}/global-effectable-lib)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../effects/global_effectable ${CMAKE_CURRENT_BINARY_DIR}/global_effectable)
