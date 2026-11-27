# Specs for deluge::io (src/deluge/io, the C++ wrapper over libdeluge/file_io.h) and the storage, favourites, recorder
# and stem-export policies built on it. The wrapper links against a hand-written in-memory mock of file_io.h's C ABI
# (tests/support/mocks/mock_file_io.cpp), not the FatFS-backed adapter, so these specs get real
# open/read/write/seek/close and directory-iteration round trips with no mounted filesystem. The bounded C paths
# (c_path.h) and the gates' output times (io/midi/midi_queue.cpp, its clock from mock_hal.cpp) are specced here too. The pad-grid crash pattern has its own driver
# (debug/).
set(_app ${DELUGE_REPO_ROOT}/src/deluge)
deluge_specs_driver(io_specs TREES host
  SOURCES
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_file_io.cpp
    mock_stream_io.cpp
    ${_app}/io/file.cpp
    ${_app}/io/stream.cpp
    # The storage channels' C ABI as "no channel here", for io/file.cpp's collectOrRequestDir.
    ${DELUGE_REPO_ROOT}/tests/support/mocks/storage_channel_mocks.cpp
    ${_app}/storage/existence_policy.cpp
    ${_app}/storage/readback_policy.cpp
    ${_app}/model/favourite/favourites_policy.cpp
    ${_app}/storage/startup_song_policy.cpp
    ${_app}/model/sample/recorder_write_policy.cpp
    ${_app}/model/sample/recorder_finalize_policy.cpp
    ${_app}/model/sample/recorder_create_policy.cpp
    ${_app}/processing/stem_export/stem_search_policy.cpp
    ${_app}/io/midi/midi_queue.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_hal.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
  INCLUDES
    ${DELUGE_REPO_ROOT}/include              # libdeluge/file_io.h, libdeluge/types.h
    ${_app}                                  # io/file.hpp
    ${DELUGE_REPO_ROOT}/src                  # definitions_cxx.hpp (storage/storage_manager.h's transitive include)
    # mock_file_io.h, and the util/firmware_version.h host stub (the real one is generated at build).
    ${DELUGE_REPO_ROOT}/tests/support/mocks
)
