configure_file(version.h.in version.h @ONLY)

add_subdirectory(gui)

set_source_files_properties(${deluge_THUMB_SOURCES}
  TARGET_DIRECTORY deluge
  PROPERTIES COMPILE_OPTIONS "-mthumb"
)

# Add the sources to the target
file(GLOB_RECURSE deluge_SOURCES CONFIGURE_DEPENDS
  deluge.cpp
  drivers/*.S
  dsp/*.S
  hid/*.S
  io/*.S
  memory/*.S
  model/*.S
  modulation/*.S
  playback/*.S
  processing/*.S
  storage/*.S
  testing/*.S
  util/*.S
  drivers/*.c
  dsp/*.c
  hid/*.c
  io/*.c
  memory*.c/
  model/*.c
  modulatio*.cn/
  playback/*.c
  processing/*.c
  storage/*.c
  testing/*.c
  util/*.c
  drivers/*.cpp
  dsp/*.cpp
  hid/*.cpp
  io/*.cpp
  memory/*.cpp
  model/*.cpp
  modulation/*.cpp
  playback/*.cpp
  processing/*.cpp
  storage/*.cpp
  testing/*.cpp
  util/*.cpp
)

target_sources(deluge PUBLIC ${deluge_SOURCES})

target_include_directories(deluge PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_LIST_DIR}
    ${SHARED_INCLUDE}
)

target_link_libraries(deluge PUBLIC
    fatfs
    NE10
    RZA1
    eyalroz_printf
    deluge_gui
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
