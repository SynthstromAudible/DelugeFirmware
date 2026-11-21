add_executable(PresetNavigationTests
    ../RunAllTests.cpp
    preset_navigation_tests.cpp
    ../../../src/deluge/gui/ui/browser/preset_navigation.cpp
)
target_compile_features(PresetNavigationTests PRIVATE cxx_std_23)
target_compile_definitions(PresetNavigationTests PRIVATE IN_UNIT_TESTS=1 CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_include_directories(PresetNavigationTests PRIVATE ../../../src ../../../src/deluge)
target_link_libraries(PresetNavigationTests PRIVATE CppUTestExt etl::etl)
add_test(NAME PresetNavigationTests COMMAND PresetNavigationTests)
set_tests_properties(PresetNavigationTests PROPERTIES LABELS "preset_navigation" TIMEOUT 30)
