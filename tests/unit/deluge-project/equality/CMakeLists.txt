# The model-equality gate: a driver built from the model and serialization libraries only, run here on the committed
# synthetic songs and on model_records.XML, a document holding every record the model decodes (written for the model;
# the app does not load it). The corpus run is the model-equality-corpus registry entry (run_corpus.sh).
deluge_test_directory(TREES host arm)
add_executable(model_equality model_equality.cpp equality.cpp ${CMAKE_CURRENT_LIST_DIR}/../model_hooks_for_tests.cpp)
target_link_libraries(model_equality PRIVATE deluge_project deluge_instrument deluge_global_effectable deluge_effects)
target_include_directories(model_equality PRIVATE ${CMAKE_CURRENT_LIST_DIR}/.. ${DELUGE_REPO_ROOT}/tests/support/mocks)
set_target_properties(model_equality PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
add_test(NAME model_equality_synthetic COMMAND model_equality ${DELUGE_REPO_ROOT}/tests/golden/synthetic
                                                             ${CMAKE_CURRENT_LIST_DIR}/model_records.XML)
