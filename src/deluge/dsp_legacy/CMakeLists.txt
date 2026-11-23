
file (GLOB_RECURSE deluge_dsp_legacy_SOURCES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.c
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
  ${CMAKE_CURRENT_LIST_DIR}/*.s
)

add_library(deluge_dsp_legacy STATIC ${deluge_dsp_legacy_SOURCES})

target_include_directories(deluge_dsp_legacy PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/..
  ${SHARED_INCLUDE}
  ${CMAKE_SOURCE_DIR}/include # <libdeluge/...> boundary headers
  ${DELUGE_CRATE_INCLUDE_DIRS} # the Rust crates' C headers the sample reader's headers include
)

target_include_directories(deluge_dsp_legacy PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
)

set_target_properties(deluge_dsp_legacy
    PROPERTIES
        C_STANDARD ${DELUGE_C_STANDARD}
        C_STANDARD_REQUIRED ON
        CXX_STANDARD ${DELUGE_CXX_STANDARD}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS ON
        UNITY_BUILD true
)

target_link_libraries(deluge_dsp_legacy PRIVATE
    eyalroz_printf
    argon
    etl::etl
)

target_compile_definitions(deluge_dsp_legacy PUBLIC
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
        $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
)

target_compile_options(deluge_dsp_legacy PRIVATE
    ${DELUGE_COMMON_COMPILE_OPTIONS}
    # use a unity build instead (combine all cpp to one before compiling), which has similar gains without the arm/thumb issues
    "-fno-lto"
    # we're not using lto so go ahead and inline
    "-finline-functions"
    # Build dsp code in ARM (not Thumb) mode — RZ/A1L only; meaningless off-target,
    # where the host build (DELUGE_HOST) compiles this same DSP via SIMDe.
    "$<$<NOT:$<BOOL:${DELUGE_HOST}>>:-marm>"
)
