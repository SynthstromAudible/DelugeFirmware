# Specs for src/deluge/modulation/sync: sync levels and swing intervals, which delegate to the plugin and
# music libraries.
deluge_specs_driver(deluge_modulation_sync_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/modulation/sync.cpp
  LIBRARIES
    etl::etl
    deluge_plugin
    deluge_music
    deluge_foundation
  INCLUDES
    ${DELUGE_REPO_ROOT}/src
    ${DELUGE_REPO_ROOT}/src/deluge
    ${DELUGE_REPO_ROOT}/include
)
