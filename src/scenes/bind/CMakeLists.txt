# deluge_scenes_bind — what a scenes menu node edits, bound to the model's records and the engines' descriptors.
#
# Standalone of the app: it depends on the model and the engine libraries only, so its specs (tests/scenes-bind) run
# against a project::Song with no engine runtime objects, and scripts/check_library_boundary.py holds it to that.
# The app supplies what it cannot see: the runtime facts (RuntimeFacts) and the sink that applies Edits (WriteSink).

add_library(deluge_scenes_bind STATIC slot_reader.cpp setting_context.cpp write_sink.cpp)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/optimise_for_size.cmake)
deluge_optimise_target_for_size(deluge_scenes_bind)
target_include_directories(deluge_scenes_bind PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_scenes_bind PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_scenes_bind PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                      CXX_EXTENSIONS ON)
endif()
target_link_libraries(deluge_scenes_bind PUBLIC deluge_project deluge_instrument)
target_compile_options(deluge_scenes_bind PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
