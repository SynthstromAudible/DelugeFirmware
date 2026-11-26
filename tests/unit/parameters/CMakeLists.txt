add_executable(ParameterTests
  RunAllTests.cpp
  param_manager_tests.cpp
  ../../32bit_unit_tests/container/lazy_param_node_vector.cpp
  mocks/memory.cpp
  mocks/firmware.cpp
  ../../../src/deluge/util/container/list/bidirectional_linked_list.cpp
  ../../../src/deluge/util/container/array/ordered_resizeable_array_with_multi_word_key.cpp
  ../../../src/deluge/modulation/params/param_manager.cpp
  ../../../src/deluge/modulation/params/param_collection.cpp
  ../../../src/deluge/modulation/params/param_set.cpp
  ../../../src/deluge/modulation/patch/patch_cable_set.cpp
  ../../../src/deluge/modulation/patch/patch_cable.cpp
  ../../../src/deluge/modulation/midi/midi_param_collection.cpp
  ../../../src/deluge/modulation/midi/midi_param_vector.cpp
  ../../../src/deluge/modulation/midi/midi_param.cpp
  ../../../src/deluge/util/d_stringbuf.cpp
  ../../../src/deluge/util/algorithm/quick_sorter.cpp
  ../../../src/deluge/modulation/automation/auto_param.cpp
  ../../../src/deluge/modulation/params/param_node_vector.cpp
  ../../../src/deluge/util/container/array/resizeable_array.cpp
  ../../../src/deluge/util/container/array/ordered_resizeable_array.cpp
)
target_compile_features(ParameterTests PRIVATE cxx_std_23)
target_compile_definitions(ParameterTests PRIVATE IN_UNIT_TESTS=1 CPPUTEST_MEM_LEAK_DETECTION_DISABLED)
target_include_directories(ParameterTests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ../../32bit_unit_tests/mocks
  ../../../src ../../../src/deluge ../../../src/NE10/inc ../../../src/lib
  ${simde_SOURCE_DIR}/simde)
target_link_libraries(ParameterTests PRIVATE CppUTestExt argon etl::etl)
add_test(NAME ParameterTests COMMAND ParameterTests)
set_tests_properties(ParameterTests PROPERTIES LABELS "parameters" TIMEOUT 30)

# Drop unrelated firmware entry points; virtual collection methods remain linked.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(ParameterTests PRIVATE -ffunction-sections -fdata-sections)
  if(APPLE)
    target_link_options(ParameterTests PRIVATE -Wl,-dead_strip)
  else()
    target_link_options(ParameterTests PRIVATE -Wl,--gc-sections)
  endif()
endif()

# Give clang-tidy an explicit C++ command for the adapter header too, instead of
# letting it infer firmware flags or a header/precompiled-header language mode.
add_library(parameter_test_headers OBJECT mocks/memory.h)
set_source_files_properties(mocks/memory.h PROPERTIES LANGUAGE CXX)
target_compile_features(parameter_test_headers PRIVATE cxx_std_23)
