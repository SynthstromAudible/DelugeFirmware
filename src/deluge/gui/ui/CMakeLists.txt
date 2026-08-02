#
# Build UI sources in their own library, without LTO.
#
# LTO on this large set of translation units causes excessive
# memory usage/build times (and in some configurations OOMs the
# linker), so the UI sources are pulled out into their own
# static library that's compiled with LTO disabled, similar to
# deluge_menu_items.
#

file(GLOB_RECURSE deluge_ui_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_library(deluge_ui STATIC ${deluge_ui_SOURCES})

add_dependencies(deluge_ui deluge_menus deluge_strings deluge_version)

target_include_directories(deluge_ui PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/../..
  ${CMAKE_CURRENT_LIST_DIR}/../../../OSLikeStuff
  ${deluge_version_INCLUDES}
  ${CMAKE_CURRENT_BINARY_DIR}/../..
  ${SHARED_INCLUDE}
)

set_target_properties(deluge_ui
    PROPERTIES
        C_STANDARD ${DELUGE_C_STANDARD}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
)

target_compile_definitions(deluge_ui PUBLIC
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

target_link_libraries(deluge_ui PRIVATE
    argon
    NE10
    RZA1
    fatfs
    eyalroz_printf
    etl::etl
)

target_compile_options(deluge_ui PRIVATE
    ${DELUGE_COMMON_COMPILE_OPTIONS}
    # Don't LTO this library: keeps build times/memory sane for this
    # large set of translation units.
    "-fno-lto"
    # we're not using lto so go ahead and inline
    "-finline-functions"
)
