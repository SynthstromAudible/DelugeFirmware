
file (GLOB_RECURSE deluge_dsp_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/*.s
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
        UNITY_BUILD true
)

target_link_libraries(deluge_dsp PRIVATE
    NE10
    eyalroz_printf
    argon
)

target_compile_definitions(deluge_dsp PUBLIC
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
        $<$<BOOL:${ENABLE_FX_BENCHMARK}>:ENABLE_FX_BENCHMARK=1>
)

target_compile_options(deluge_dsp PRIVATE
    ${DELUGE_COMMON_COMPILE_OPTIONS}
    # use a unity build instead (combine all cpp to one before compiling), which has similar gains without the arm/thumb issues
    "-fno-lto"
    # we're not using lto so go ahead and inline
    "-finline-functions"
    # Build dsp code with arm mode
    "-marm"
)
