include_guard(GLOBAL)

include(${CMAKE_CURRENT_LIST_DIR}/library_warnings.cmake)

# deluge_library_target(<target>)
#
# The flags every library below the app builds with: a unity build, no LTO (so -finline-functions does the inlining)
# and ARM (not Thumb) mode on hardware. The app keeps Thumb and -fno-inline-functions; library code must not, since the
# app's flags cost DSP code its inlining (the mod-matrix spike measured its evaluator 15 % slower under them).
function(deluge_library_target target)
  set_target_properties(${target} PROPERTIES
    C_STANDARD ${DELUGE_C_STANDARD}
    C_STANDARD_REQUIRED ON
    CXX_STANDARD ${DELUGE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
    UNITY_BUILD true
  )
  target_compile_definitions(${target} PUBLIC
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:IN_HARDWARE_DEBUG=1>
    $<$<CONFIG:DEBUG,RELWITHDEBINFO>:ENABLE_TEXT_OUTPUT=1>
  )
  deluge_library_warnings(${target})
  target_compile_options(${target} PRIVATE
    ${DELUGE_COMMON_COMPILE_OPTIONS}
    "-fno-lto"
    "-finline-functions"
    "$<$<NOT:$<BOOL:${DELUGE_HOST}>>:-marm>"
  )
endfunction()
