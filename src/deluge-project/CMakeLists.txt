# deluge_project — the project model: a song's persistent musical state as host data (deluge::project).
#
# Standalone: it depends only on the standalone libraries below (plus foundation/panic.h), and
# scripts/check_library_boundary.py holds it to that with no allowlist. What a host supplies — the memory model
# containers use, its out-of-memory exception, and the edit guard around structural edits the render may observe —
# comes through the link-time hooks in model_memory.h and edit_guard.h.

file(GLOB_RECURSE deluge_project_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_library(deluge_project STATIC ${deluge_project_SOURCES})
# The model's codecs and edits run on the UI tier, off the audio path.
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_project)

# Headers are included as "deluge-project/..."; the include root is src/ (this directory's parent).
target_include_directories(deluge_project PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
target_compile_features(deluge_project PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_project PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                  CXX_EXTENSIONS ON)
endif()

target_link_libraries(deluge_project PUBLIC deluge_plugin_types deluge_serialization deluge_music_theory)

target_compile_options(deluge_project PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
