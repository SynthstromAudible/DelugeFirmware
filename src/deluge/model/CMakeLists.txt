target_sources(deluge PUBLIC
    model_stack.cpp
    output.cpp
    timeline_counter.cpp
)
add_subdirectory(action)
add_subdirectory(clip)
add_subdirectory(consequence)
add_subdirectory(drum)
add_subdirectory(global_effectable)
add_subdirectory(instrument)
add_subdirectory(mod_controllable)
add_subdirectory(note)
add_subdirectory(sample)
add_subdirectory(settings)
add_subdirectory(song)
add_subdirectory(voice)
