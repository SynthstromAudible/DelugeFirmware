# Specs for src/engine-api/ (the types layer and the engine API), added by both the x86 (tests/unit) and ARM
# (tests/unit-arm) test trees.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engine-api/types ${CMAKE_CURRENT_BINARY_DIR}/engine-types-lib)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/engine-api ${CMAKE_CURRENT_BINARY_DIR}/engine-api-lib)
deluge_specs_driver(engine_types_specs LIBRARIES deluge_engine_api)
