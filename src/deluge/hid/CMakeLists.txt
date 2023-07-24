target_sources(deluge PUBLIC
    button.cpp
    buttons.cpp
    encoder.cpp
    encoders.cpp
    hid_sysex.cpp
)
add_subdirectory(display)
add_subdirectory(led)
add_subdirectory(matrix)
