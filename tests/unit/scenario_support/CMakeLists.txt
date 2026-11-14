# tests/unit/scenario_support/CMakeLists.txt
#
# The scenarios' shared support (tests/integration/scenarios/support): the failure report and the per-step wait.
# Pure C++ with no app dependency, so it builds here against the source the host harness compiles.
deluge_specs_driver(scenario_support_specs TREES host
  SOURCES
    ../../integration/scenarios/support/scenario_report.cpp
  INCLUDES
    ../../integration # scenarios/support/scenario_report.h
)
