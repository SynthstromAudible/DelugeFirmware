# Specs for the dependency-free foundation library (src/foundation). oom_for_tests.cpp supplies deluge::oom() as a
# std::bad_alloc throw, so a spec can observe the infallible path.
deluge_specs_driver(foundation_specs TREES host
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/oom_for_tests.cpp
  LIBRARIES deluge_spec deluge_midi)
