configure_file(../version.h.in version.h @ONLY)

file (GLOB_RECURSE deluge_GUI_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_library(deluge_gui STATIC ${deluge_GUI_SOURCES})

target_include_directories(deluge_gui PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/..
  ${SHARED_INCLUDE}
)

set_target_properties(deluge_gui
    PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        SUFFIX "${EXECUTABLE_VERSION_SUFFIX}.elf"
)

target_link_libraries(deluge_gui PUBLIC
    NE10
    eyalroz_printf
)

target_compile_options(deluge_gui PUBLIC
    $<$<AND:$<BOOL:${MAINTAINER_MODE}>,$<CXX_COMPILER_ID:GNU>>:-fanalyzer> # static analysis

    # Warnings
    $<$<BOOL:${MAINTAINER_MODE}>:-Wextra>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wall>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wabi>
    $<$<BOOL:${MAINTAINER_MODE}>:-Wpedantic>
    $<$<CONFIG:DEBUG>:-Wnull-dereference>
    $<$<CONFIG:DEBUG>:-Wno-psabi>
    $<$<CONFIG:DEBUG>:-Wno-unused-parameter>

    $<$<CONFIG:DEBUG>:-Werror=write-strings>

    # Offsetof for non standard types is supported in GCC, 
    # if another compiler does not support it they are obligated to error

    # Stack usage
    # tracked for diagnostic usage. We have a 32k stack so it's not that important
    # primarily concerned with unbounded stack use
    $<$<CONFIG:DEBUG>:-Wstack-usage=2000>
    $<$<CONFIG:RELEASE,RELWITHDEBINFO>:-Wstack-usage=2000>

    # Build GUI code with thumb mode
    "-mthumb"
)


