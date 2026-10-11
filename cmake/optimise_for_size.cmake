# Builds code off the audio path for size in the device's optimised configs, where the image must fit
# in on-chip SRAM (docs/dev/image_size.md). -Oz survives ThinLTO as each function's `minsize`
# attribute. Host trees keep their usual level: the build uses -funsafe-math-optimizations, so a
# different level could move a golden.
include_guard(GLOBAL)

# deluge_optimise_target_for_size(<target>): every source of <target>.
function(deluge_optimise_target_for_size target)
  if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    target_compile_options(${target} PRIVATE "$<$<CONFIG:Release,RelWithDebInfo>:-Oz>")
  endif()
endfunction()

# deluge_optimise_sources_for_size(<source>...): these sources, in whichever target builds them.
function(deluge_optimise_sources_for_size)
  if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    set_property(SOURCE ${ARGN} APPEND PROPERTY COMPILE_OPTIONS "$<$<CONFIG:Release,RelWithDebInfo>:-Oz>")
  endif()
endfunction()
