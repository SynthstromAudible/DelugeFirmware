# tests/unit/sync/CMakeLists.txt
#
# deluge::sync specs. Exercises the RAII scope primitives directly against the real
# storage_op.cpp translation unit (which owns the relocated permit-flag definition) --
# no card, no FatFS, no resource manager.
deluge_specs_driver(sync_specs
  SOURCES
    ../../../src/deluge/sync/storage_op.cpp
    ../../../src/deluge/sync/sd_access.cpp
  INCLUDES
    ../../../include    # libdeluge/types.h
    ../../../src/deluge # sync/storage_op.h
)
