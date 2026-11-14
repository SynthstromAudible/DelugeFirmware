# Specs for the Deluge instrument (src/instruments/deluge) and for the global-effectable table
# (src/effects/global_effectable), whose specs check it against the instrument's table; added by both the x86
# (tests/unit) and ARM (tests/unit-arm) test trees.
add_subdirectory(deluge)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../effects/global_effectable ${CMAKE_CURRENT_BINARY_DIR}/global_effectable)
