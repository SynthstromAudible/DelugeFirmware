configure_file(version.h.in version.h @ONLY)

file (GLOB_RECURSE deluge_gui_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_library(deluge_gui STATIC ${deluge_gui_SOURCES})

target_include_directories(deluge_gui PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/..
  ${SHARED_INCLUDE}
)

target_include_directories(deluge_gui PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
)

set_target_properties(deluge_gui
    PROPERTIES
        C_STANDARD ${DELUGE_C_STANDARD}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_link_libraries(deluge_gui PUBLIC
    NE10
    eyalroz_printf
)

target_compile_options(deluge_gui PUBLIC
    ${DELUGE_COMMON_COMPILE_OPTIONS}

    # Build GUI code with thumb mode
    "-mthumb"
)


