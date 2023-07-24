target_sources(deluge PUBLIC
    file_item.cpp
    flash_storage.cpp
    storage_manager.cpp
)
add_subdirectory(audio)
add_subdirectory(cluster)
add_subdirectory(multi_range)
add_subdirectory(wave_table)
