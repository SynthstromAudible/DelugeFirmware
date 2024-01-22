
file (GLOB_RECURSE deluge_dsp_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_library(deluge_dsp STATIC ${deluge_dsp_SOURCES})

target_include_directories(deluge_dsp PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/..
  ${SHARED_INCLUDE}
)

target_include_directories(deluge_dsp PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
)

set_target_properties(deluge_dsp
    PROPERTIES
        C_STANDARD ${DELUGE_C_STANDARD}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_link_libraries(deluge_dsp PUBLIC
    NE10
    eyalroz_printf
)

target_compile_options(deluge_dsp PRIVATE
    ${DELUGE_COMMON_COMPILE_OPTIONS}

    # Build dsp code with thumb mode
    "-marm"
)
