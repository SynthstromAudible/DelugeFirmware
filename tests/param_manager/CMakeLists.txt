# The parent tests/CMakeLists.txt already sets the language standard and calls enable_testing().
add_executable(ParamManagerLayoutTests param_manager_layout_tests.cpp)
target_include_directories(ParamManagerLayoutTests PRIVATE mocks ../../src ../../src/deluge)
target_compile_features(ParamManagerLayoutTests PRIVATE cxx_std_23)
add_test(NAME ParamManagerLayoutTests COMMAND ParamManagerLayoutTests)
