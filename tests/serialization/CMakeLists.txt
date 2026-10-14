# Specs for src/serialization/, added by both the x86 (tests/unit) and ARM (tests/unit-arm) test trees.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/serialization ${CMAKE_CURRENT_BINARY_DIR}/serialization-lib)
deluge_specs_driver(serialization_specs LIBRARIES deluge_serialization)
