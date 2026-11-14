# Specs for the global-effectable table (src/effects/global_effectable), which they check against the Deluge
# instrument's table.
deluge_specs_driver(global_effectable_specs TREES host arm
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_panic.cpp
  LIBRARIES deluge_global_effectable deluge_instrument
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
