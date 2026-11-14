# Specs for src/deluge/gui/ui/slice_core.cpp: the slicer's audition through the real SliceCore, in the world
# mock_slicer_env.cpp builds for it, so no other spec shares this binary. DEFINITIONS: libstdc++'s parallel backend
# (oneTBB) pulls in <immintrin.h>, which collides with SIMDe's NEON names; the firmware has no TBB, so the serial
# backend.
deluge_specs_driver(deluge_gui_ui_slice_core_specs TREES host NEON
  SOURCES
    ${DELUGE_REPO_ROOT}/src/deluge/gui/ui/slice_core.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_slicer_env.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_display.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_hal.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_print.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/c_string.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/synth_noise.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/runtime/app_loop.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_song.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_storage.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/storage/file_reader.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/storage/Deserializer.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/storage/JsonDeserializer.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/io/file.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/storage_channel_mocks.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_file_io.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_firmware_version.cpp
  LIBRARIES
    etl::etl
    deluge_gfx_oled
    deluge_gfx_grid
    deluge_ui_core
    deluge_ui_oled
    deluge_ui_grid
    deluge_notify
    deluge_serialization
  INCLUDES
    ${DELUGE_REPO_ROOT}/src
    ${DELUGE_REPO_ROOT}/src/deluge
    ${DELUGE_REPO_ROOT}/include
    ${DELUGE_REPO_ROOT}/tests/support/mocks
  DEFINITIONS
    _GLIBCXX_USE_TBB_PAR_BACKEND=0
)
