# deluge_host_chain: the param table a host chain has (the song master's, a kit's affect-entire's and an audio
# track's) and the chains' knob pages. On the plugin types and the effects' words and setting params.
add_library(deluge_host_chain STATIC pages.cpp)
target_include_directories(deluge_host_chain PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_host_chain PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_host_chain PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                            CXX_EXTENSIONS ON)
endif()
# deluge_effects for the headers its own headers include (effects/words.h, effects/setting_params.h), whose include
# root it carries: PUBLIC, as every includer of pages.h needs them. Its code calls nothing compiled there.
target_link_libraries(deluge_host_chain PUBLIC deluge_plugin_types deluge_plugin_api deluge_effects)
target_compile_options(deluge_host_chain PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/library_warnings.cmake)
deluge_library_warnings(deluge_host_chain)
