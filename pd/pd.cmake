# Find Pure Data headers
if(WIN32)
  set(PD_DIR "C:/Program Files/Pd/")
  set(LIBRARY_EXT "dll")
elseif(APPLE)
  set(PD_DIR "/Applications/Pd-extended.app/Contents/Resources")
  set(LIBRARY_EXT "so")
else()
  set(PD_DIR "/usr/local/lib/pd")
  set(LIBRARY_EXT "so")
endif()

if(NOT EXISTS "${PD_DIR}/bin/pd.${LIBRARY_EXT}")
  message(FATAL_ERROR "Pure Data not found in ${PD_DIR}. Please set PD_DIR to the correct path.")
endif()

add_library(pd SHARED IMPORTED)
set_target_properties(pd PROPERTIES
  IMPORTED_LOCATION ${PD_DIR}/bin/pd.${LIBRARY_EXT}
  INTERFACE_INCLUDE_DIRECTORIES ${PD_DIR}/src
)

if(WIN32)
  set_target_properties(pd PROPERTIES IMPORTED_IMPLIB ${PD_DIR}/bin/pd.lib)
endif()
