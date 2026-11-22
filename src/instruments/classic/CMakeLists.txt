# deluge_instruments_classic: the Deluge instrument plugin — its state, its words and its codec. Built on the plugin
# API and the effects.
add_library(deluge_instruments_classic STATIC cable_acceptance.cpp codec.cpp cable_codec.cpp deluge_synth.cpp mod_matrix.cpp naming.cpp pages.cpp settings.cpp
                                     state_access.cpp state_rules.cpp composed_state.cpp record_edit.cpp)
target_include_directories(deluge_instruments_classic PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../..)
target_compile_features(deluge_instruments_classic PUBLIC cxx_std_23)
if(DEFINED DELUGE_CXX_STANDARD)
  set_target_properties(deluge_instruments_classic PROPERTIES CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON
                                                     CXX_EXTENSIONS ON)
endif()
target_link_libraries(deluge_instruments_classic PUBLIC deluge_plugin deluge_effects)
target_compile_options(deluge_instruments_classic PUBLIC ${DELUGE_COMMON_COMPILE_OPTIONS})
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/library_warnings.cmake)
deluge_library_warnings(deluge_instruments_classic)
