# deluge_global_effectable: the params the song master, a kit's affect-entire and an audio track share, as one
# provisional table until each effect declares its own, and the chains' knob pages. On the plugin types and the
# effects' words and setting params.
add_library(deluge_global_effectable STATIC pages.cpp)
target_include_directories(deluge_global_effectable PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_global_effectable PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_global_effectable PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                            CXX_EXTENSIONS ON)
endif()
# deluge_effects for the headers its own headers include (effects/words.h, effects/setting_params.h), whose include
# root it carries: PUBLIC, as every includer of pages.h needs them. Its code calls nothing compiled there.
target_link_libraries(deluge_global_effectable PUBLIC deluge_plugin_types deluge_plugin_api deluge_effects)
target_compile_options(deluge_global_effectable PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/library_warnings.cmake)
deluge_library_warnings(deluge_global_effectable)
