target_sources(deluge PUBLIC
    audio_output.cpp
    source.cpp
)
add_subdirectory(engines)
add_subdirectory(live)
add_subdirectory(metronome)
add_subdirectory(sound)
