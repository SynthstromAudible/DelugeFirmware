# tests/unit/dsp/CMakeLists.txt
#
# deluge::dsp render-kernel specs. Drives the real oscillator kernel on the host
# so degenerate-input crashes (divide-by-zero on a stalled phase increment) are
# caught here rather than on hardware.
#
# The argon/SIMDe block below mirrors tests/unit/audio_stream/CMakeLists.txt --
# oscillator.cpp reaches <argon.hpp> transitively through wave_table.h ->
# storage/audio/stream/convert.h, so this driver needs the same backend setup.
# Keep the pins in sync with lib/CMakeLists.txt and that sibling driver.
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG 5897db725c1cdee452b5f288614796364468f2ea
)
FetchContent_MakeAvailable(argon)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(DSP_SPEC_NEON_NATIVE ON)
endif()

if(NOT DSP_SPEC_NEON_NATIVE)
  FetchContent_Declare(simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde
    GIT_TAG v0.8.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(simde)
  set(DSP_SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})
  # Parent builds this tree at -m32 on plain Linux, where SIMDe's native path
  # drags in AVX512/FMA headers that don't parse in 32-bit mode.
  if(UNIX AND NOT APPLE)
    add_compile_definitions(SIMDE_NO_NATIVE)
  endif()
endif()

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs}
    # The real kernel under test, plus the wave tables and render helpers it
    # calls. mocks.cpp stands in for the firmware globals it reads.
    ../../../src/deluge/dsp/oscillators/oscillator.cpp
    ../../../src/deluge/dsp/oscillators/basic_waves.cpp
    ../../../src/deluge/dsp/oscillators/sine_osc.cpp
    ../../../src/deluge/util/lookuptables/lookuptables.cpp
    ../../../src/deluge/util/lookuptables/analog_square.cpp
    ../../../src/deluge/util/lookuptables/saw.cpp
    ../../../src/deluge/util/lookuptables/square.cpp
    ../../../src/deluge/util/lookuptables/mystery_synth_a_saw.cpp
    ../../../src/deluge/util/lookuptables/mystery_synth_b_saw.cpp
    mocks.cpp
  )
  target_link_libraries(${driver_name} PRIVATE c++spec argon)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ../../../include
    ../../../src
    ../../../src/deluge
  )
  if(NOT DSP_SPEC_NEON_NATIVE)
    target_include_directories(${driver_name} PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/../../../sim/compat
      ${DSP_SIMDE_INCLUDE_DIR}
      ${DSP_SIMDE_INCLUDE_DIR}/simde
    )
  endif()
  set_target_properties(${driver_name} PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(NAME ${spec_name} COMMAND ${driver_name} ${spec_name} --verbose)
  endforeach()
endfunction()

create_specs_driver(dsp_specs ${CMAKE_CURRENT_LIST_DIR})
