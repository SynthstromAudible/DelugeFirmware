# Specs for deluge::sync (src/deluge/sync): sd_busy() against the real sd_access.cpp, with no card, no FatFS and no
# resource manager.
deluge_specs_driver(sync_specs TREES host
  SOURCES ${DELUGE_REPO_ROOT}/src/deluge/sync/sd_access.cpp
  INCLUDES
    ${DELUGE_REPO_ROOT}/include     # libdeluge/types.h
    ${DELUGE_REPO_ROOT}/src/deluge  # sync/sd_access.h
)
