# Specs for src/deluge/modulation/sync: sync levels and swing intervals against the song stand-in. IN_UNIT_TESTS makes
# sync.cpp take mock_song.h's Song, which no other spec's world shares, so these build their own driver.
# mock_display.cpp formats numbers with intToString, so the driver links deluge_foundation.
deluge_specs_driver(deluge_modulation_sync_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/modulation/sync.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_song.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_display.cpp
  LIBRARIES
    etl::etl
    deluge_plugin_api
    deluge_foundation
  INCLUDES
    ${DELUGE_REPO_ROOT}/src
    ${DELUGE_REPO_ROOT}/src/deluge
    ${DELUGE_REPO_ROOT}/include
    ${DELUGE_REPO_ROOT}/tests/support/mocks
  DEFINITIONS
    IN_UNIT_TESTS=1
)
