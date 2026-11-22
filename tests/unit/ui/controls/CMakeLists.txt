# Specs for the controls (src/ui/controls), built against the standalone libraries' include roots only, so a hidden
# dependency on the app fails this build. The model's host hooks come from model_hooks_for_tests.cpp.
deluge_specs_driver(controls_specs TREES host arm
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/model_hooks_for_tests.cpp
          ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_panic.cpp
  LIBRARIES deluge_ui_controls deluge_project deluge_instruments_classic deluge_host_chains
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks)
