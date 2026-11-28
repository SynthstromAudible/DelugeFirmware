# The clean-roundtrip gate: a driver built from the model and serialization libraries only, run here on the committed
# synthetic songs and on model_records.XML, and on the song corpus by run_corpus.sh (the clean-roundtrip-corpus
# registry entry). schema_lock prints the clean format's schema for the schema-lock gate.
deluge_test_directory(TREES host arm)
set(clean_mocks ${DELUGE_REPO_ROOT}/tests/support/mocks/model_hooks_for_tests.cpp)
set(clean_libraries deluge_project deluge_instruments_classic deluge_host_chains deluge_effects)
add_executable(clean_roundtrip clean_roundtrip.cpp clean_check.cpp ${clean_mocks})
target_link_libraries(clean_roundtrip PRIVATE ${clean_libraries})
target_include_directories(clean_roundtrip PRIVATE ${CMAKE_CURRENT_LIST_DIR}/.. ${DELUGE_REPO_ROOT}/tests/support/mocks)
set_target_properties(clean_roundtrip PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
add_test(NAME clean_roundtrip_synthetic
         COMMAND clean_roundtrip ${DELUGE_REPO_ROOT}/tests/golden/songs/synthetic
                 ${CMAKE_CURRENT_LIST_DIR}/../equality/model_records.XML)
add_executable(schema_lock schema_lock.cpp ${clean_mocks})
target_link_libraries(schema_lock PRIVATE ${clean_libraries})
target_include_directories(schema_lock PRIVATE ${DELUGE_REPO_ROOT}/tests/support/mocks)
set_target_properties(schema_lock PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
