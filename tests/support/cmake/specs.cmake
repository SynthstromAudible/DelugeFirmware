# The test trees' shared CMake: the trees and how a test directory names the ones that build it, one CppSpec driver per
# spec directory, the directories a tree builds, and argon's NEON backend for the specs that compile SIMD code.
#
# A tree's root sets DELUGE_TEST_TREE and its compile options, then includes this file: what it makes available is
# built with those options (the host tree is -m32 on Linux).
include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/golden.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/deps.cmake)

get_filename_component(DELUGE_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)
set(DELUGE_SPECS_MAIN_IN ${CMAKE_CURRENT_LIST_DIR}/specs_main.cpp.in)

# The trees: the -m32 host tree, the ARM tree cross-built and run under qemu, and the host's UBSan tree.
set(DELUGE_TEST_TREES host arm ubsan)
if(NOT DELUGE_TEST_TREE IN_LIST DELUGE_TEST_TREES)
  message(FATAL_ERROR "set DELUGE_TEST_TREE to one of ${DELUGE_TEST_TREES} before including specs.cmake "
                      "(it is '${DELUGE_TEST_TREE}')")
endif()

FetchContent_MakeAvailable(CppSpec argon)
deluge_argon_headers_are_system()

# An ARM target has a real <arm_neon.h>: the arm-linux cross (tests/trees/arm, run under qemu) checks the A9's own
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

# Opt-in dynamic analysis for a tree's spec drivers: -DDELUGE_TEST_SANITIZERS=ON.
#
# AddressSanitizer (heap/stack overflow, use-after-free, double-free) and UndefinedBehaviorSanitizer both work in the
# -m32 host build (the i386 libasan/libubsan runtimes are required: on Arch, `lib32-gcc-libs`). LeakSanitizer (ASan's
# leak-at-exit detector) is x86-64 only and does nothing under -m32, so host leak checking is done through the
# TrackingAllocator's outstanding-allocation set (tests/unit/deluge/memory/tracking_allocator_spec.cpp) instead.
option(DELUGE_TEST_SANITIZERS "Build the spec drivers with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

# Each spec driver precompiles CppSpec's headers once instead of every spec parsing them: ~1.1 s less per spec TU, and
# 30-40% less summed compile time for dsp_specs. -DDELUGE_TEST_PCH=OFF builds them as before.
option(DELUGE_TEST_PCH "Precompile CppSpec's headers for each spec driver" ON)

# Applies the sanitizers to one target. Nothing is instrumented globally: the CppSpec and etl dependencies are fetched
# third-party code, and a sanitizer report from inside them is not a finding about ours. ASan's runtime intercepts
# malloc/free process-wide regardless, so the uninstrumented framework objects are still covered for heap errors.
function(deluge_apply_test_sanitizers target)
  if(DELUGE_TEST_SANITIZERS AND UNIX AND NOT APPLE)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer
                                             -fno-sanitize-recover=all)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endif()
endfunction()

# Fails the configure unless every argument after `what` names a tree; `what` says which declaration.
function(_deluge_check_trees what)
  foreach(tree IN LISTS ARGN)
    if(NOT tree IN_LIST DELUGE_TEST_TREES)
      message(FATAL_ERROR "${what}: unknown tree '${tree}'; the trees are ${DELUGE_TEST_TREES}")
    endif()
  endforeach()
endfunction()

# deluge_specs_driver(<name> TREES <tree>... [NEON] [ONLY_HOST <spec>...] [ONLY_ARM <spec>...] [ONLY_UBSAN <spec>...]
#                     [SOURCES <file>...] [LIBRARIES <lib>...] [INCLUDES <dir>...] [DEFINITIONS <def>...])
#
# One executable holding the *_spec.cpp files under the calling directory, down to the next directory with its own
# CMakeLists.txt, and one ctest test per spec, named after its file. TREES names the trees that build the directory's
# specs; write the tree names on TREES's own line, where deluge_spec_directories reads them. A spec listed under
# ONLY_<TREE> (its path below the calling directory) is built by that tree alone, whatever TREES says; a tree left with
# no spec defines nothing, so a call on <name> after this one goes inside if(TARGET <name>). A spec's entry point is
# named after its file's stem (CPPSPEC_SPEC(<stem minus _spec>), or DELUGE_SPEC(<stem>, ...) from spec_main.h) wherever
# below the directory the file sits, so a spec moves without an edit. SOURCES are the code under test and any mocks;
# relative paths resolve against the calling directory. Every spec includes against the calling directory and then
# tests/, so a header from another test directory is named by its path there ("support/golden_update.h",
# "harness/ui_script.h", "unit/deluge/dsp-next/raw_words.h"). NEON links argon as deluge_test_neon() does.
# DEFINITIONS are compile definitions for the driver's own sources (the specs and SOURCES).
# Every executable gets GOLDEN_ROOT (golden.cmake).
function(deluge_specs_driver name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "NEON" ""
    "TREES;ONLY_HOST;ONLY_ARM;ONLY_UBSAN;SOURCES;LIBRARIES;INCLUDES;DEFINITIONS")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "deluge_specs_driver(${name}): unexpected arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT arg_TREES AND NOT arg_ONLY_HOST AND NOT arg_ONLY_ARM AND NOT arg_ONLY_UBSAN)
    message(FATAL_ERROR "deluge_specs_driver(${name}): name the trees that build it: TREES <${DELUGE_TEST_TREES}>...")
  endif()
  _deluge_check_trees("deluge_specs_driver(${name})" ${arg_TREES})
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
  # The specs this tree builds: when TREES names it, every spec but those another tree alone builds; otherwise only
  # those this tree alone builds.
  set(only_elsewhere "")
  foreach(tree IN LISTS DELUGE_TEST_TREES)
    string(TOUPPER ${tree} upper)
    foreach(spec IN LISTS arg_ONLY_${upper})
      if(NOT spec IN_LIST spec_sources)
        message(FATAL_ERROR "deluge_specs_driver(${name}): ONLY_${upper} names ${spec}, which is no spec here")
      endif()
      if(NOT tree STREQUAL DELUGE_TEST_TREE)
        list(APPEND only_elsewhere ${spec})
      endif()
    endforeach()
  endforeach()
  if(DELUGE_TEST_TREE IN_LIST arg_TREES)
    foreach(spec IN LISTS only_elsewhere)
      list(REMOVE_ITEM spec_sources ${spec})
    endforeach()
  else()
    string(TOUPPER ${DELUGE_TEST_TREE} upper)
    set(spec_sources ${arg_ONLY_${upper}})
  endif()
  if(NOT spec_sources)
    return()
  endif()
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
  # Every spec compiles at the standard the firmware's libraries build at, and checks the standard library's
  # preconditions (span and container bounds among them), so a read past an end fails the spec rather than going unseen
  # at the trees' optimisation levels.
  set_target_properties(${name} PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)
  target_compile_definitions(${name} PRIVATE _GLIBCXX_ASSERTIONS ${arg_DEFINITIONS})
  deluge_golden_root(${name})
  target_link_libraries(${name} PRIVATE c++spec ${arg_LIBRARIES})
  # The precompiled header holds CppSpec's macro-free headers only. cppspec.hpp stays out: its `context`, `$` and `_`
  # macros must come after the project headers (spec_main.h), and with #pragma once the later include of cppspec.hpp
  # adds just the macros. Only the specs get it: SOURCES (the code under test and mocks) never include CppSpec, and
  # force-including it would let them compile with a missing include of their own that only a PCH-off build shows. A
  # spec that needs per-source flags must skip it too (SKIP_PRECOMPILE_HEADERS), or clang refuses the mismatch.
  #
  # ccache refuses a compile that uses a precompiled header unless its sloppiness allows pch_defines and time_macros,
  # so the driver's compiles (its specs and SOURCES) go through a launcher that sets those two and nothing else:
  # include_file_mtime/ctime would let ccache cache a compile whose header changed while it ran. It replaces any
  # sloppiness set in ccache.conf for these compiles. time_macros means a cached spec keeps its __DATE__/__TIME__;
  # none uses them. Library targets keep ccache's default checks.
  if(DELUGE_TEST_PCH)
    target_precompile_headers(${name} PRIVATE <class_description.hpp> <argparse.hpp> <runner.hpp>
                                              <formatters/progress.hpp> <formatters/verbose.hpp>
                                              <formatters/junit_xml.hpp>)
    if(arg_SOURCES)
      set_source_files_properties(${arg_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
    if(DELUGE_CCACHE_PROGRAM AND CMAKE_CXX_COMPILER_LAUNCHER STREQUAL DELUGE_CCACHE_PROGRAM)
      set_property(TARGET ${name} PROPERTY CXX_COMPILER_LAUNCHER ${CMAKE_COMMAND} -E env
        CCACHE_SLOPPINESS=pch_defines,time_macros ${DELUGE_CCACHE_PROGRAM})
    endif()
  endif()
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${DELUGE_REPO_ROOT}/tests ${arg_INCLUDES})
  if(arg_NEON)
    deluge_test_neon(${name})
  endif()
  deluge_apply_test_sanitizers(${name})
  foreach(stem IN LISTS stems)
    add_test(NAME ${stem} COMMAND ${name} ${stem} --verbose)
  endforeach()
endfunction()

# deluge_test_directory(TREES <tree>...)
#
# Names the trees that build a test directory holding no spec driver (a standalone driver, a codegen probe).
# deluge_spec_directories reads the declaration from the file; the call checks the names.
function(deluge_test_directory)
  cmake_parse_arguments(PARSE_ARGV 0 arg "" "" "TREES")
  if(arg_UNPARSED_ARGUMENTS OR NOT arg_TREES)
    message(FATAL_ERROR "${CMAKE_CURRENT_SOURCE_DIR}: write deluge_test_directory(TREES <${DELUGE_TEST_TREES}>...)")
  endif()
  _deluge_check_trees("deluge_test_directory in ${CMAKE_CURRENT_SOURCE_DIR}" ${arg_TREES})
endfunction()

# Sets <out> to the text of the CMakeLists.txt at <file> with its comments removed: a # at the start of a line or
# after whitespace, to the end of the line. A # inside a word or a quoted argument ("x#y") is kept.
function(_deluge_read_list_file file out)
  file(READ ${file} text)
  string(REGEX REPLACE "(^|[ \t\r\n])#[^\n]*" "\\1" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

# deluge_spec_directories(<dir>)
#
# Adds every directory below <dir> whose CMakeLists.txt names this tree, in path order, each built into
# <binary dir>/<dir's name>/<its path below dir>. A file names its trees in a deluge_specs_driver(<name> TREES ...) or
# deluge_test_directory(TREES ...) call, as the words after TREES on TREES's own line (TREES itself may follow the
# name on the next line), or with an ONLY_<TREE> keyword; comments are not read. Every CMakeLists.txt below <dir> must
# name its trees, whichever directory it belongs to, since all of them are read: one that names no tree, or an
# unknown one, fails the configure, because no tree would build it and a test that never runs looks like one that
# passed. So does a *_spec.cpp whose nearest CMakeLists.txt between it and <dir> declares no deluge_specs_driver, or
# that has none, since no driver would compile it. Every file read, and the set of spec files, is a configure
# dependency, so a directory that starts naming this tree, or a new spec, is picked up by the next build.
function(deluge_spec_directories base)
  get_filename_component(_base ${base} ABSOLUTE)
  get_filename_component(_base_name ${_base} NAME)
  string(TOUPPER ${DELUGE_TEST_TREE} _this_tree)
  file(GLOB_RECURSE _specs CONFIGURE_DEPENDS ${_base}/*_spec.cpp)
  foreach(_spec IN LISTS _specs)
    cmake_path(GET _spec PARENT_PATH _dir)
    set(_driver "")
    while(NOT _dir STREQUAL _base)
      if(EXISTS ${_dir}/CMakeLists.txt)
        set(_driver ${_dir}/CMakeLists.txt)
        break()
      endif()
      cmake_path(GET _dir PARENT_PATH _dir)
    endwhile()
    if(NOT _driver)
      message(FATAL_ERROR "${_spec}: no CMakeLists.txt in its directory or a parent below ${_base}, so no driver "
                          "builds it: declare deluge_specs_driver(<name> TREES ...) in one")
    endif()
    _deluge_read_list_file(${_driver} _text)
    if(NOT _text MATCHES "deluge_specs_driver\\(")
      message(FATAL_ERROR "${_spec}: ${_driver}, the nearest CMakeLists.txt above it, declares no "
                          "deluge_specs_driver, so no driver builds it")
    endif()
  endforeach()
  file(GLOB_RECURSE _list_files CONFIGURE_DEPENDS ${_base}/CMakeLists.txt)
  list(REMOVE_ITEM _list_files ${_base}/CMakeLists.txt)
  list(SORT _list_files)
  foreach(_list_file IN LISTS _list_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_list_file})
    _deluge_read_list_file(${_list_file} _text)
    set(_driver_call "deluge_specs_driver\\([ \t\r\n]*[A-Za-z0-9_]+[ \t\r\n]+")
    set(_directory_call "deluge_test_directory\\([ \t\r\n]*")
    string(REGEX MATCHALL "(${_driver_call}|${_directory_call})TREES([ \t]+[a-z]+)+" _declarations "${_text}")
    string(REGEX MATCHALL "(^|[^A-Za-z0-9_])ONLY_[A-Z]+" _only "${_text}")
    if(NOT _declarations AND NOT _only)
      message(FATAL_ERROR "${_list_file} names no tree: declare deluge_specs_driver(<name> TREES ...) or "
                          "deluge_test_directory(TREES ...)")
    endif()
    set(_wanted FALSE)
    foreach(_declaration IN LISTS _declarations)
      string(REGEX REPLACE "^.*TREES" "" _declaration "${_declaration}")
      string(REGEX MATCHALL "[a-z]+" _trees "${_declaration}")
      _deluge_check_trees(${_list_file} ${_trees})
      if(DELUGE_TEST_TREE IN_LIST _trees)
        set(_wanted TRUE)
      endif()
    endforeach()
    foreach(_keyword IN LISTS _only)
      string(REGEX REPLACE "^.*ONLY_" "" _tree "${_keyword}")
      string(TOLOWER ${_tree} _tree)
      _deluge_check_trees(${_list_file} ${_tree})
      if(_tree STREQUAL DELUGE_TEST_TREE)
        set(_wanted TRUE)
      endif()
    endforeach()
    if(_wanted)
      cmake_path(GET _list_file PARENT_PATH _dir)
      cmake_path(RELATIVE_PATH _dir BASE_DIRECTORY ${_base} OUTPUT_VARIABLE _rel)
      add_subdirectory(${_dir} ${CMAKE_BINARY_DIR}/${_base_name}/${_rel})
    endif()
  endforeach()
endfunction()
