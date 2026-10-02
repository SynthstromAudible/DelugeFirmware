# tests/unit/sync/CMakeLists.txt
#
# deluge::sync specs. Exercises sd_busy() directly against the real sd_access.cpp
# translation unit -- no card, no FatFS, no resource manager.
deluge_specs_driver(sync_specs
  SOURCES
    ../../../src/deluge/sync/sd_access.cpp
  INCLUDES
    ../../../include    # libdeluge/types.h
    ../../../src/deluge # sync/sd_access.h
)
