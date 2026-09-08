# The parent tests/CMakeLists.txt already sets the language standard and calls enable_testing().
add_executable(ParamManagerLayoutTests param_manager_layout_tests.cpp)
target_include_directories(ParamManagerLayoutTests PRIVATE mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerLayoutTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerLayoutTests COMMAND ParamManagerLayoutTests)

add_executable(ParamManagerCleanupTests param_manager_cleanup_tests.cpp
	../../src/deluge/modulation/params/param_manager_cleanup.cpp)
target_include_directories(ParamManagerCleanupTests PRIVATE mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerCleanupTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerCleanupTests COMMAND ParamManagerCleanupTests)
