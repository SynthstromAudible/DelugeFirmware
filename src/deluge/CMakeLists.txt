configure_file(util/firmware_version.h.in util/firmware_version.h @ONLY)
add_subdirectory(dsp)
add_subdirectory(gui/l10n)
add_subdirectory(gui/menu_item/generate)
add_subdirectory(version)

# Add the sources to the target
file(GLOB_RECURSE deluge_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.S
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_dependencies(deluge deluge_strings deluge_version)

# Filter out DSP sources as those are built as part of the deluge_dsp library
list(FILTER deluge_SOURCES EXCLUDE REGEX .*/dsp/.*)

# Ensure the menu sources are built prior to building the main target
add_dependencies(deluge deluge_menus)

target_sources(deluge PUBLIC
  ${deluge_SOURCES}
  # Add strings-sources explicitly: since they're build artefacts clean nukes them,
  # and if they're not there in the first place then the source glob won't find them,
  # and they only get generated not built.
  ${deluge_strings_SOURCES}
)

target_include_directories(deluge PUBLIC
    ${deluge_version_INCLUDES}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
    ${SHARED_INCLUDE}
)

target_link_libraries(deluge PUBLIC
    fatfs
    NE10
    RZA1
    eyalroz_printf
    deluge_dsp
)

target_compile_definitions(deluge PUBLIC
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

if(ENABLE_RTT)
    message(STATUS "RTT enabled for deluge")
    target_compile_definitions(deluge PUBLIC
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:HAVE_RTT=1>
    )
    target_link_libraries(deluge PUBLIC
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:RTT>
    )
endif(ENABLE_RTT)

if(ENABLE_SYSEX_LOAD)
    message(STATUS "Sysex firmware loading enabled for deluge")
    target_compile_definitions(deluge PUBLIC ENABLE_SYSEX_LOAD=1)
endif(ENABLE_SYSEX_LOAD)

if(ENABLE_MATRIX_DEBUG)
    message(STATUS "Extra pad matrix debug enabled for deluge")
    target_compile_definitions(deluge PUBLIC ENABLE_MATRIX_DEBUG=1)
endif(ENABLE_MATRIX_DEBUG)

if(ENABLE_FX_BENCHMARK)
    target_compile_definitions(deluge PUBLIC ENABLE_FX_BENCHMARK=1)
endif(ENABLE_FX_BENCHMARK)

