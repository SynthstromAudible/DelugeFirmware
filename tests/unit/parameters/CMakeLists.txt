add_executable(ParameterTests ../RunAllTests.cpp ../../32bit_unit_tests/container/lazy_param_node_vector.cpp mocks/memory.cpp
  ../../../src/deluge/util/container/list/bidirectional_linked_list.cpp
  ../../../src/deluge/util/container/array/ordered_resizeable_array_with_multi_word_key.cpp
  ../../../src/deluge/modulation/params/param_node_vector.cpp
  ../../../src/deluge/util/container/array/resizeable_array.cpp
  ../../../src/deluge/util/container/array/ordered_resizeable_array.cpp
)
target_compile_features(ParameterTests PRIVATE cxx_std_23)
target_compile_definitions(ParameterTests PRIVATE IN_UNIT_TESTS=1 CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_include_directories(ParameterTests PRIVATE
  ../../32bit_unit_tests/mocks
  ../../../src ../../../src/deluge ../../../src/NE10/inc ../../../src/lib
  ${simde_SOURCE_DIR}/simde)
target_link_libraries(ParameterTests PRIVATE CppUTestExt argon etl::etl)
add_test(NAME ParameterTests COMMAND ParameterTests)
set_tests_properties(ParameterTests PROPERTIES LABELS "parameters" TIMEOUT 30)
