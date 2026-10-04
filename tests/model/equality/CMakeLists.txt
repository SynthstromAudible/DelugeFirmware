# The model-equality gate: a driver built from the model and serialization libraries only, run here on the committed
# synthetic songs (the corpus run is the model-equality-corpus registry entry).
add_executable(model_equality model_equality.cpp equality.cpp ${CMAKE_CURRENT_LIST_DIR}/../model_hooks_for_tests.cpp
                              ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks/mock_panic.cpp)
target_link_libraries(model_equality PRIVATE deluge_project)
target_include_directories(model_equality PRIVATE ${CMAKE_CURRENT_LIST_DIR}/.. ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks)
set_target_properties(model_equality PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
add_test(NAME model_equality_synthetic COMMAND model_equality ${CMAKE_CURRENT_LIST_DIR}/../../golden/synthetic)
