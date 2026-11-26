# Specs for the engine's side of USB audio over a render block's USB port (src/deluge/io/usb/usb_audio.cpp). Its own
# driver: its few links outside itself (the flash setting, the device start) are stood in by the spec.
set(_app ${DELUGE_REPO_ROOT}/src/deluge)
deluge_specs_driver(io_usb_specs TREES host
  SOURCES
    ${_app}/io/usb/usb_audio.cpp
  INCLUDES
    ${DELUGE_REPO_ROOT}/include
    ${_app}
    ${DELUGE_REPO_ROOT}/src
    ${DELUGE_REPO_ROOT}/tests/support/mocks
)
