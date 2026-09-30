# tests/unit/io/CMakeLists.txt
#
# deluge::io (idiomatic C++ wrapper over libdeluge/file_io.h) specs. Links the
# wrapper against a hand-written in-memory mock of file_io.h's C-ABI (mock_file_io.cpp),
# NOT the real FatFS-backed adapter (that's tests/unit/spec/'s concern, for the adapter's
# own tests) -- so these specs get real open/read/write/seek/close and
# directory-iteration round-trip coverage with no mounted filesystem required.
deluge_specs_driver(io_specs
  SOURCES
    mock_file_io.cpp
    mock_stream_io.cpp
    ../../../src/deluge/io/file.cpp
    ../../../src/deluge/io/stream.cpp
    # The storage channels' C ABI as "no channel here", for io/file.cpp's collectOrRequestDir.
    ../../support/mocks/storage_channel_mocks.cpp
    ../../../src/deluge/storage/existence_policy.cpp
    ../../../src/deluge/storage/readback_policy.cpp
    ../../../src/deluge/model/favourite/favourites_policy.cpp
    ../../../src/deluge/storage/startup_song_policy.cpp
    ../../../src/deluge/model/sample/recorder_write_policy.cpp
    ../../../src/deluge/model/sample/recorder_finalize_policy.cpp
    ../../../src/deluge/model/sample/recorder_create_policy.cpp
    ../../../src/deluge/processing/stem_export/stem_search_policy.cpp
  INCLUDES
    ../../../include         # libdeluge/file_io.h, libdeluge/types.h
    ../../../src/deluge      # io/file.hpp
    ../../../src             # definitions_cxx.hpp (storage/storage_manager.h's transitive include)
    ../../support/mocks      # util/firmware_version.h host stub (the real one is generated at firmware-build time)
)
