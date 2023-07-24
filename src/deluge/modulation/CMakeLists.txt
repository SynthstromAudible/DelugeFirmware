target_sources(deluge PUBLIC
    arpeggiator.cpp
    envelope.cpp
    lfo.cpp
)
add_subdirectory(automation)
add_subdirectory(midi)
add_subdirectory(params)
add_subdirectory(patch)
