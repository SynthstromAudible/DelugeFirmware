# deluge_midi — the MIDI protocol library.
#
# Pure and dependency-free (libc + STL only): the MIDI message representation and
# the (de)serialization spec — DIN byte streams and USB-MIDI 1.0 event packets. It
# knows the protocol, not the hardware: the BSP/HAL move opaque byte buffers and do
# not link it. Used by the application and by the host test build (cppspec), so it
# is written as modern STL-based C++26.
add_library(deluge_midi STATIC
    message.cpp
    usb_event.cpp
)

# Headers are included as "midi/..."; the include root is src/ (this dir's parent).
# Self-contained (not via SHARED_INCLUDE) so it builds in both the firmware and the
# standalone test project.
target_include_directories(deluge_midi PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)

set_target_properties(deluge_midi PROPERTIES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
)
