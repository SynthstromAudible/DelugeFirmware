# Specs for src/deluge/storage/song_preview: the preview's decode against the real deserializer stack on an
# in-memory card (mock_file_io.cpp, mock_storage.cpp), and the preview window's bound, so no other spec shares this
# binary. DEFINITIONS: DELUGE_REPO_ROOT, under which both read the checked-in JSON song fixture.
deluge_specs_driver(deluge_storage_song_preview_specs TREES host NEON
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/storage/song_preview.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/storage/file_reader.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/runtime/app_loop.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/storage/Deserializer.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/storage/JsonDeserializer.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/io/file.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/c_string.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/storage_channel_mocks.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_file_io.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_storage.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_firmware_version.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_hal.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_print.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_display.cpp
  LIBRARIES
    etl::etl
    deluge_serialization
    deluge_base
  INCLUDES
    ${DELUGE_REPO_ROOT}/src
    ${DELUGE_REPO_ROOT}/src/deluge
    ${DELUGE_REPO_ROOT}/include
    ${DELUGE_REPO_ROOT}/tests/support/mocks
  DEFINITIONS
    DELUGE_REPO_ROOT="${DELUGE_REPO_ROOT}"
)
