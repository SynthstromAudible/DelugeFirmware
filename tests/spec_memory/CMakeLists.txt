#
# Allocator conformance specs (CppSpec).
#
# A standalone spec driver isolated from tests/spec: it links the full memory
# subsystem (which pulls in operators.cpp's global new/delete override, gated
# behind IN_UNIT_TESTS) plus the audio-engine/display mocks. Keeping it in its
# own binary means that override never leaks into the other specs. Mirrors the
# 32bit_unit_tests memory link surface, but with a CppSpec driver instead of
# CppUTest so the conformance suite is reusable against the future Rust core.
#
include(FetchContent)
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

add_compile_definitions(
  IN_UNIT_TESTS=1
)

# The allocator and its support sources.
file(GLOB_RECURSE deluge_mm_SOURCES
  ../../src/deluge/memory/*
)

file(GLOB_RECURSE deluge_SOURCES
  # Used for prints / l10n error strings.
  ../../src/deluge/gui/l10n/*
  # Used by most modules (containers, etc).
  ../../src/deluge/util/*
  ../../src/lib/printf.c
)
list(APPEND deluge_SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../src/foundation/string_format.c)

# Reuse the existing mock implementations (audio_engine, display, libdeluge, ...).
file(GLOB_RECURSE mock_SOURCES
  ../32bit_unit_tests/mocks/*
)

function(create_specs_driver driver_name spec_dir)
  file(GLOB_RECURSE spec_sources RELATIVE ${spec_dir} ${spec_dir}/*_spec.cpp)
  create_test_sourcelist(specs ${driver_name}.cpp ${spec_sources})
  add_executable(${driver_name} ${specs})
  target_sources(${driver_name} PRIVATE ${deluge_mm_SOURCES} ${deluge_SOURCES} ${mock_SOURCES})
  target_link_libraries(${driver_name} PRIVATE c++spec)
  target_include_directories(${driver_name} PRIVATE
    mocks
    ../32bit_unit_tests/mocks
    ../../src/deluge
    ../../src/OSLikeStuff
    ../../src/NE10/inc
    ../../src/lib
    ../../src
    ../../include       # libdeluge/* boundary headers
    ../../src/bsp/rza1  # relocated drivers/* headers
  )
  set_target_properties(${driver_name} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS ON
  )
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY spec_name)
    add_test(
      NAME ${spec_name}
      COMMAND ${driver_name} ${spec_name} --verbose
    )
  endforeach()
endfunction()

create_specs_driver(memory_specs ${CMAKE_CURRENT_LIST_DIR})
