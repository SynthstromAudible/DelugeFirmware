# The unit trees' shared CMake: one CppSpec driver per suite directory, and argon's NEON backend for the suites that
# compile SIMD code.
#
# Include it after the tree's compile options are set: what it makes available is built with them (the host unit
# tree is -m32 on Linux).
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deps.cmake)

set(DELUGE_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(DELUGE_SPECS_MAIN_IN ${CMAKE_CURRENT_LIST_DIR}/specs_main.cpp.in)

FetchContent_MakeAvailable(CppSpec argon)
deluge_argon_headers_are_system()

# An ARM target has a real <arm_neon.h>: the arm-linux cross (tests/unit-arm, run under qemu) checks the A9's own
# instructions, and an arm64 host its own; only other hosts reach NEON through SIMDe.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|arm64|aarch64)$")
  set(DELUGE_TEST_NEON_NATIVE ON)
endif()

# Link `target` against argon. A host with a real <arm_neon.h> needs nothing more. Any other host reaches NEON through
# SIMDe, set up the way the host sim sets it up (sim/CMakeLists.txt), so a spec exercises the same backend the
# golden renders through: sim/compat's <arm_neon.h> shim, SIMDe's root for <simde/arm/neon.h> and its simde/
# subdirectory for argon's bare <arm/neon.h>. On Linux the tree is -m32, where SIMDe's native path pulls in
# AVX512/FMA headers that do not parse in 32-bit mode, and the portable fallback is what the golden captures were
# made against, so SIMDE_NO_NATIVE selects it.
function(deluge_test_neon target)
  target_link_libraries(${target} PRIVATE argon)
  if(DELUGE_TEST_NEON_NATIVE)
    return()
  endif()
  FetchContent_MakeAvailable(simde)
  FetchContent_GetProperties(simde)
  target_include_directories(${target} PRIVATE
    ${DELUGE_REPO_ROOT}/sim/compat
    ${simde_SOURCE_DIR}
    ${simde_SOURCE_DIR}/simde
  )
  if(UNIX AND NOT APPLE)
    target_compile_definitions(${target} PRIVATE SIMDE_NO_NATIVE)
  endif()
endfunction()

# deluge_specs_driver(<name> [NEON] [SOURCES <file>...] [LIBRARIES <lib>...] [INCLUDES <dir>...])
#
# One executable holding every *_spec.cpp under the calling directory, and one ctest test per spec, named after its
# file. A spec's entry point is named after its file's stem (CPPSPEC_SPEC(<stem minus _spec>), or
# DELUGE_SPEC(<stem>, ...) from spec_main.h) wherever below the directory the file sits, so a spec moves without an
# edit. SOURCES are the code under test and any mocks; relative paths resolve against the calling directory. NEON
# links argon as deluge_test_neon() does.
function(deluge_specs_driver name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "NEON" "" "SOURCES;LIBRARIES;INCLUDES")
  # CONFIGURE_DEPENDS: a new spec file is picked up by the next build. Without it the new spec compiles into nothing
  # until someone reconfigures, and a spec that never ran looks exactly like one that passed.
  file(GLOB_RECURSE spec_sources CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/*_spec.cpp)
  # A subdirectory with its own CMakeLists.txt builds its own driver, so its specs are not this one's.
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec PARENT_PATH dir)
    while(dir)
      if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt)
        list(REMOVE_ITEM spec_sources ${spec})
        break()
      endif()
      cmake_path(GET dir PARENT_PATH dir)
    endwhile()
  endforeach()
  # The entry table: one row per spec, keyed by its stem. Two specs with one stem would be one ctest name and one
  # symbol, so they are refused here rather than at link time.
  set(stems "")
  set(declarations "")
  set(rows "")
  foreach(spec IN LISTS spec_sources)
    cmake_path(GET spec STEM LAST_ONLY stem)
    if(stem IN_LIST stems)
      message(FATAL_ERROR "deluge_specs_driver(${name}): two specs named ${stem} under ${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    list(APPEND stems ${stem})
    string(APPEND declarations "int ${stem}(int, char**);\n")
    string(APPEND rows "\t{\"${stem}\", &${stem}},\n")
  endforeach()
  set(DELUGE_SPEC_DECLARATIONS "${declarations}")
  set(DELUGE_SPEC_ROWS "${rows}")
  configure_file(${DELUGE_SPECS_MAIN_IN} ${CMAKE_CURRENT_BINARY_DIR}/${name}_main.cpp @ONLY)
  add_executable(${name} ${CMAKE_CURRENT_BINARY_DIR}/${name}_main.cpp ${spec_sources} ${arg_SOURCES})
  target_link_libraries(${name} PRIVATE c++spec ${arg_LIBRARIES})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${arg_INCLUDES})
  set_target_properties(${name} PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
  if(arg_NEON)
    deluge_test_neon(${name})
  endif()
  foreach(stem IN LISTS stems)
    add_test(NAME ${stem} COMMAND ${name} ${stem} --verbose)
  endforeach()
endfunction()
