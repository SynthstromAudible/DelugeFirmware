# Specs for src/engine-api/types/, added by both the x86 (tests/unit) and ARM (tests/unit-arm) test trees.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engine-api/types ${CMAKE_CURRENT_BINARY_DIR}/engine-types-lib)
deluge_specs_driver(engine_types_specs LIBRARIES deluge_engine_types)
