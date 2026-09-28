# tests/spec_sample_reader/CMakeLists.txt
#
# deluge::sample::SampleFrameReader specs. Links the sim-feature build of the Rust
# umbrella (deluge_rust_rs_sim, defined by the parent tests/CMakeLists.txt) so the bridge's
# deluge_sample_reader_*/deluge_sample_read calls resolve against the REAL C-ABI, plus trivial
# link-time stand-ins (mock_streaming_fill.cpp) for the app-provided accessors that ABI reaches
# into (see that file's own doc for why they're needed even though this spec's one scenario never
# runs them). The sim-feature reader pulls in deluge_sample_fill's `app_convert` shim (deluge_sample_
# convert), which itself needs the REAL deluge::audio::stream::convert_word/stitch_boundaries C++
# symbols -- so convert.cpp/stitch.cpp/audio_format_helpers.cpp (+ argon/SIMDe) are linked in here
# too, exactly like tests/spec_audio_stream/CMakeLists.txt already does for its own convert/stitch
# specs (this file's argon/SIMDe setup is copied from there verbatim -- keep the two in sync).
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

# argon (house NEON/SIMDe abstraction) -- convert.cpp includes "argon.hpp". Pin matches
# spec_audio_stream/CMakeLists.txt (and lib/CMakeLists.txt) so this driver builds against the same
# argon revision as everything else.
FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG 5897db725c1cdee452b5f288614796364468f2ea
)
FetchContent_MakeAvailable(argon)

# SIMDe backend selection -- mirrors spec_audio_stream/CMakeLists.txt (itself mirroring
# sim/CMakeLists.txt:186-214) exactly.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
  set(SAMPLE_READER_SPEC_NEON_NATIVE ON)
endif()

if(NOT SAMPLE_READER_SPEC_NEON_NATIVE)
  FetchContent_Declare(simde
    GIT_REPOSITORY https://github.com/simd-everywhere/simde
    GIT_TAG v0.8.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(simde)
  set(SAMPLE_READER_SIMDE_INCLUDE_DIR ${simde_SOURCE_DIR})
  if(UNIX AND NOT APPLE)
    add_compile_definitions(SIMDE_NO_NATIVE)
  endif()
endif()

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name}
    ${specs}
    mock_streaming_fill.cpp
    ../support/mocks/mock_critical_section.cpp
    ../../src/deluge/model/sample/sample_reader_bridge.cpp
    ../../src/deluge/storage/audio/stream/convert.cpp
    ../../src/deluge/storage/audio/stream/stitch.cpp
    ../../src/deluge/util/audio_format_helpers.cpp
  )
  target_link_libraries(${driver_name} PRIVATE c++spec argon deluge_rust_rs_sim
    # System libs the Rust std staticlib references.
    pthread dl m)
  target_include_directories(${driver_name} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../include      # libdeluge/sample_reader.h, streaming_fill.h, system.h
    ${CMAKE_CURRENT_LIST_DIR}/../../src          # definitions_cxx.hpp
    ${CMAKE_CURRENT_LIST_DIR}/../../src/deluge    # model/sample/sample_reader_bridge.h, storage/audio/stream/chunk_residency.h
    ${CMAKE_CURRENT_LIST_DIR}/../../crates/deluge_resource/include # deluge_resource.h (sample_peek_spec.cpp's own manager setup)
  )
  if(NOT SAMPLE_READER_SPEC_NEON_NATIVE)
    target_include_directories(${driver_name} PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/../../sim/compat        # <arm_neon.h> -> SIMDe shim
      ${SAMPLE_READER_SIMDE_INCLUDE_DIR}                # compat shim's <simde/arm/neon.h> -> SIMDe
      ${SAMPLE_READER_SIMDE_INCLUDE_DIR}/simde           # argon's bare <arm/neon.h> -> SIMDe
    )
  endif()
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 26
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS ON
  )
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(NAME ${spec_name} COMMAND ${driver_name} ${spec_name} --verbose)
  endforeach()
endfunction()

create_specs_driver(sample_reader_specs ${CMAKE_CURRENT_LIST_DIR})
