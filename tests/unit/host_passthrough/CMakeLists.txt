# tests/unit/host_passthrough/CMakeLists.txt
#
# Specs for the C-host POSIX passthrough (src/bsp/host/host_efatfs_passthrough.cpp): the strong
# deluge_efatfs_* definitions the sim links, tested directly against a temp dir under DELUGE_SD_ROOT.
deluge_specs_driver(host_passthrough_specs
  SOURCES
    ../../../src/bsp/host/host_efatfs_passthrough.cpp
  INCLUDES
    ../../../include # libdeluge/file_io.h, stream_io.h, streaming_fill.h
)
