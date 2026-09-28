# tests/unit/audio_stream/CMakeLists.txt
#
# deluge::audio::stream ReadSource specs. Exercises the seam via an in-memory MockReadSource
# (header-only) — no card, no FatFS, no resource manager.
include(FetchContent)
FetchContent_MakeAvailable(CppSpec)

# argon (house NEON/SIMDe abstraction) — convert.cpp includes "argon.hpp" for the vectorized
# conversion loops.
FetchContent_MakeAvailable(argon)

# SIMDe backend selection — mirrors sim/CMakeLists.txt:186-214 exactly, and MUST keep matching it:
# the whole point of testing convert.cpp's SIMD path here is that it exercises the SAME
# vcvtq_n_s32_f32/vrev/vtbl backend the golden (`./dbt sim`) renders through. A native-NEON host
# (arm64 Linux, Apple Silicon) has a real <arm_neon.h> and needs none of this; every other host
# needs SIMDe, and — critically — the same bitness/SIMDE_NO_NATIVE state the default host-sim uses
# (plain x86-64 Linux -> -m32, SIMDe's portable-C fallback, bit-accurate to NEON).
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(AUDIO_STREAM_SPEC_NEON_NATIVE ON)
endif()

if(NOT AUDIO_STREAM_SPEC_NEON_NATIVE)
  FetchContent_MakeAvailable(simde)
  # Repo layout is ${simde_SOURCE_DIR}/simde/{arm,x86,...}: the root resolves <simde/arm/neon.h>
  # (the sim/compat shim below), the simde/ subdir resolves argon's bare <arm/neon.h> directly.
  set(AUDIO_STREAM_SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})

  # tests/unit/CMakeLists.txt (the parent) already builds this whole tree at -m32 on plain Linux (to
  # match the firmware's pointer model — see its comment) — the SAME condition under which
  # sim/CMakeLists.txt's default host-sim build sets DELUGE_SIM_M32. Force SIMDe's portable-C
  # fallback there too: under -m32 SIMDe's native path drags in GCC's AVX512/FMA <immintrin.h>
  # sub-headers, which don't parse in 32-bit mode, and the portable path is what the golden's
  # bit-exact captures were made against. On x86-64 (Apple, or an explicit 64-bit host build) this
  # is skipped and SIMDe lowers to real SSE/AVX — fine, since this driver isn't a golden oracle.
  if(UNIX AND NOT APPLE)
    add_compile_definitions(SIMDE_NO_NATIVE)
  endif()
endif()

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs}
    # convert_spec.cpp exercises the real convert_word implementation (not a mock), so its
    # translation unit must be linked in. stitch_spec.cpp exercises the real stitch_boundaries
    # (which itself calls convert_word), so stitch.cpp must be linked too.
    ../../../src/deluge/storage/audio/stream/convert.cpp
    ../../../src/deluge/storage/audio/stream/stitch.cpp
    ../../../src/deluge/util/audio_format_helpers.cpp
  )
  target_link_libraries(${driver_name} PRIVATE c++spec argon)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ../../../include      # libdeluge/types.h
    ../../../src         # definitions_cxx.hpp
    ../../../src/deluge   # storage/audio/stream/read_source.h, io/stream.hpp
  )
  if(NOT AUDIO_STREAM_SPEC_NEON_NATIVE)
    target_include_directories(${driver_name} PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/../../../sim/compat        # <arm_neon.h> -> SIMDe shim
      ${AUDIO_STREAM_SIMDE_INCLUDE_DIR}                 # compat shim's <simde/arm/neon.h> -> SIMDe
      ${AUDIO_STREAM_SIMDE_INCLUDE_DIR}/simde            # argon's bare <arm/neon.h> -> SIMDe
    )
  endif()
  set_target_properties(${driver_name} PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(NAME ${spec_name} COMMAND ${driver_name} ${spec_name} --verbose)
  endforeach()
endfunction()

create_specs_driver(audio_stream_specs ${CMAKE_CURRENT_LIST_DIR})
