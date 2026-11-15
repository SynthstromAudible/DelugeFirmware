# Specs for src/music-theory/.
deluge_specs_driver(music_theory_specs TREES host arm
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_panic.cpp
  LIBRARIES deluge_music_theory
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks ${DELUGE_REPO_ROOT}/src)
