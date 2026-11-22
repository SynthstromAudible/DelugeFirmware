# Specs for src/music/.
deluge_specs_driver(music_theory_specs TREES host arm
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_panic.cpp
  LIBRARIES deluge_music
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks ${DELUGE_REPO_ROOT}/src)
