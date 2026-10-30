# Specs for src/plugin-api/ (the types layer and the plugin API), added by both the x86 (tests/unit) and ARM
# (tests/unit-arm) test trees.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/plugin-api/types ${CMAKE_CURRENT_BINARY_DIR}/plugin-types-lib)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../src/plugin-api ${CMAKE_CURRENT_BINARY_DIR}/plugin-api-lib)
deluge_specs_driver(plugin_api_specs LIBRARIES deluge_plugin_api)
