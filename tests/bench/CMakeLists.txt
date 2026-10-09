# Specs for the app's DSP benchmark runner (src/deluge/bench), added by both the x86 (tests/unit) and ARM
# (tests/unit-arm) test trees. The runner is portable: it reaches the counters only through its Counters interface,
# which the spec fakes, so it builds alone from its own sources.
deluge_specs_driver(bench_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../src/deluge/bench/runner.cpp
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../src/deluge)
