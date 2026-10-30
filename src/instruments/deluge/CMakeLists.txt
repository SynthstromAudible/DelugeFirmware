# deluge_instrument: the Deluge instrument plugin — its state, its words and its codec. Built on the plugin API and the
# effects.
add_library(deluge_instrument STATIC codec.cpp cable_codec.cpp mod_matrix.cpp naming.cpp settings.cpp)
target_include_directories(deluge_instrument PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_instrument PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_instrument PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                     CXX_EXTENSIONS ON)
endif()
target_link_libraries(deluge_instrument PUBLIC deluge_plugin_api deluge_effects)
target_compile_options(deluge_instrument PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
