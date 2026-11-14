# Specs for the scenarios' shared support (tests/integration/scenarios/support): the failure report and the per-step
# wait. Pure C++ with no app dependency, so it builds here against the source the host harness compiles. The
# value matchers every spec may use (tests/support/matchers) are specced with the harness, in tests/unit/harness.
deluge_specs_driver(scenario_support_specs TREES host
  SOURCES ${DELUGE_REPO_ROOT}/tests/integration/scenarios/support/scenario_report.cpp
  INCLUDES ${DELUGE_REPO_ROOT}/tests/integration  # scenarios/support/scenario_report.h
)
