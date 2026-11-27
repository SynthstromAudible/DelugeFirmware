# The src/ libraries the spec directories link, each added once per tree before any spec directory: a directory only
# links, so no directory depends on another having been added first. Every one is EXCLUDE_FROM_ALL, so a tree builds
# only the libraries something in it links.
#
# A tree's root include()s this after specs.cmake (deluge_test_neon) and after its compile options, which the libraries
# are built with. It sets SHARED_INCLUDE, DELUGE_HOST, DELUGE_TEST_UNSAFE_MATH_OPTIONS, DELUGE_TEST_FMA_OPTIONS and
# deluge_test_library_variant in the root's scope, where the libraries and the spec directories read them, so it is
# include()d, never add_subdirectory()d.
include_guard(GLOBAL)

set(_deluge_src ${DELUGE_REPO_ROOT}/src)
set(_deluge_libs ${CMAKE_BINARY_DIR}/libs)
# The include root the src/ libraries name each other's headers from.
cmake_path(SET SHARED_INCLUDE ${_deluge_src})
# Vendored headers (<gsl/gsl>) come from third_party/, as system includes.
include_directories(SYSTEM ${DELUGE_REPO_ROOT}/third_party)
# The firmware's standard and warning set (DELUGE_CXX_STANDARD, DELUGE_COMMON_COMPILE_OPTIONS), read by every library
# below as the device build reads them, so each builds here at the standard and with the warnings it ships with. A root
# that has already included them (the host tree) keeps its own.
if(NOT DEFINED DELUGE_CXX_STANDARD)
  include(${DELUGE_REPO_ROOT}/cmake/deluge_common_options.cmake)
endif()

# The standalone engine libraries, which the host and the ARM tree both test: serialization, music, the plugin
# contract and its types, the effects, the host chains' table, the classic instrument, the project model and the
# host's waveform cache; the controls and the waveform view are added here only off the host tree, which gets them
# through src/ui. Each is built with only the standalone libraries' include roots, so a hidden dependency on the app
# fails the tree's build.
add_subdirectory(${_deluge_src}/serialization ${_deluge_libs}/serialization EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/music ${_deluge_libs}/music EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/plugin/types ${_deluge_libs}/plugin-types EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/plugin ${_deluge_libs}/plugin EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/effects ${_deluge_libs}/effects EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/arp ${_deluge_libs}/arp EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/host/chains ${_deluge_libs}/host-chains EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/instruments/classic ${_deluge_libs}/instruments-classic EXCLUDE_FROM_ALL)
add_subdirectory(${_deluge_src}/project ${_deluge_libs}/project EXCLUDE_FROM_ALL)
# The host tree gets the controls and the waveform view through src/ui below.
if(NOT DELUGE_TEST_TREE STREQUAL "host")
  add_subdirectory(${_deluge_src}/ui/controls ${_deluge_libs}/ui-controls EXCLUDE_FROM_ALL)
  add_subdirectory(${_deluge_src}/ui/views/waveform ${_deluge_libs}/ui-views-waveform EXCLUDE_FROM_ALL)
endif()

if(DELUGE_TEST_TREE STREQUAL "host")
  # The host tree's own: printf and MIDI for the app's specs; the scenes' binding to the model; the UI's strings
  # (gui/l10n); the UI libraries under src/ui (gfx, widgets, controls, shell, the waveform view), which include from
  # SHARED_INCLUDE; and the level-3 UI script runner (tests/harness/level3).
  add_subdirectory(${DELUGE_REPO_ROOT}/third_party/printf ${_deluge_libs}/printf EXCLUDE_FROM_ALL)
  add_subdirectory(${_deluge_src}/midi ${_deluge_libs}/midi EXCLUDE_FROM_ALL)
  add_subdirectory(${_deluge_src}/ui/scenes/bind ${_deluge_libs}/ui-scenes-bind EXCLUDE_FROM_ALL)
  add_subdirectory(${_deluge_src}/deluge/gui/l10n ${_deluge_libs}/l10n EXCLUDE_FROM_ALL)
  add_subdirectory(${_deluge_src}/ui ${_deluge_libs}/ui EXCLUDE_FROM_ALL)
  # The compressor's menu page and the knob push reach argon through effects/.
  deluge_test_neon(deluge_ui_scenes)
  add_subdirectory(${DELUGE_REPO_ROOT}/tests/harness/level3 ${_deluge_libs}/level3 EXCLUDE_FROM_ALL)
endif()

# The DSP library (deluge_dsp_fixed, deluge_dsp_float) and the synth library (deluge_synth), built as the firmware builds them,
# and what they reach outside themselves. Spec directories link them; none compiles their sources. A host tree off Linux
# builds only the triangle distribution spec of the dsp specs (tests/unit/dsp), which links none of them.
#
# Off the A9 they build as the host sim builds them: DELUGE_HOST leaves out the library flags' ARM mode and the DX7's
# NEON kernel. The ARM tree builds them as the device does, the DX7's modern operators through
# fixed/dx7/neon_fm_kernel.s.
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
  set(DELUGE_HOST ON)
endif()
FetchContent_MakeAvailable(etl)
add_subdirectory(${DELUGE_REPO_ROOT}/src/base ${CMAKE_BINARY_DIR}/libs/base EXCLUDE_FROM_ALL)
add_subdirectory(${DELUGE_REPO_ROOT}/src/dsp ${CMAKE_BINARY_DIR}/libs/dsp EXCLUDE_FROM_ALL)
add_subdirectory(${DELUGE_REPO_ROOT}/src/synth ${CMAKE_BINARY_DIR}/libs/synth EXCLUDE_FROM_ALL)

# The app's code the DSP library reaches outside itself, and the panic contract: the lookup tables, the noise stream, and a
# freeze that records rather than halts. On the device these are app objects; here they are one archive, so a driver
# pulls in only what its code reaches. Nothing in it depends on a library.
add_library(deluge_test_dsp_closure STATIC EXCLUDE_FROM_ALL
  ${DELUGE_REPO_ROOT}/src/deluge/util/lookuptables/lookuptables.cpp
  ${DELUGE_REPO_ROOT}/src/deluge/util/synth_noise.cpp
  ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_panic.cpp)
target_include_directories(deluge_test_dsp_closure PRIVATE
  ${DELUGE_REPO_ROOT}/src/deluge ${DELUGE_REPO_ROOT}/src ${DELUGE_REPO_ROOT}/tests/support/mocks)
# The synth library's: the param math, which reads the DSP library's table interpolation.
add_library(deluge_test_synth_closure STATIC EXCLUDE_FROM_ALL
  ${DELUGE_REPO_ROOT}/src/deluge/util/param_functions.cpp)
target_include_directories(deluge_test_synth_closure PRIVATE ${DELUGE_REPO_ROOT}/src/deluge ${DELUGE_REPO_ROOT}/src)
target_link_libraries(deluge_test_synth_closure PRIVATE deluge_dsp_fixed)
set_target_properties(deluge_test_dsp_closure deluge_test_synth_closure PROPERTIES
  CXX_STANDARD ${DELUGE_CXX_STANDARD} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS ON)

# Each library names the archives it reaches, link-only, so every link line puts them after it. The synth library
# reaches the effects' saturation.
target_link_libraries(deluge_dsp_fixed PRIVATE $<LINK_ONLY:deluge_test_dsp_closure>)
target_link_libraries(deluge_dsp_float PRIVATE $<LINK_ONLY:deluge_test_dsp_closure>)
target_link_libraries(deluge_synth PRIVATE
  $<LINK_ONLY:deluge_test_synth_closure> $<LINK_ONLY:deluge_effects> $<LINK_ONLY:deluge_test_dsp_closure>)

# The dsp, synth and effects libraries and their closures check the standard library's preconditions, span bounds among
# them: a read past the end of a span in their kernels fails the spec instead of going unseen at the trees' optimisation
# levels. Each gets argon's backend (deluge_test_neon): the effects' sidechain reaches argon through the kernel core,
# which a host without NEON reaches through SIMDe.
foreach(library deluge_dsp_fixed deluge_dsp_float deluge_synth deluge_effects deluge_test_dsp_closure
                deluge_test_synth_closure)
  deluge_test_neon(${library})
  target_compile_definitions(${library} PRIVATE _GLIBCXX_ASSERTIONS)
endforeach()

# deluge_test_library_variant(<library> <suffix> OPTIONS <option>...)
#
# <library>_<suffix>: <library> built again from its own sources, with its own properties and usage requirements, plus
# OPTIONS. A spec driver that holds code to other compile flags links the variant, so the library code it reaches is
# built under those flags too.
function(deluge_test_library_variant library suffix)
  cmake_parse_arguments(PARSE_ARGV 2 arg "" "" "OPTIONS")
  set(variant ${library}_${suffix})
  get_target_property(sources ${library} SOURCES)
  get_target_property(source_dir ${library} SOURCE_DIR)
  list(TRANSFORM sources PREPEND ${source_dir}/ REGEX "^[^/$]")
  add_library(${variant} STATIC EXCLUDE_FROM_ALL ${sources})
  foreach(property COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES INCLUDE_DIRECTORIES LINK_LIBRARIES
                   INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_OPTIONS INTERFACE_INCLUDE_DIRECTORIES
                   INTERFACE_LINK_LIBRARIES C_STANDARD C_STANDARD_REQUIRED CXX_STANDARD CXX_STANDARD_REQUIRED
                   CXX_EXTENSIONS UNITY_BUILD)
    get_target_property(value ${library} ${property})
    if(NOT value STREQUAL "value-NOTFOUND")
      set_property(TARGET ${variant} PROPERTY ${property} ${value})
    endif()
  endforeach()
  target_compile_options(${variant} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${arg_OPTIONS}>")
endfunction()

# The flag sets the dsp specs are held to beside the tree's own: the device's float flags, and the fused multiply-add an
# FMA target runs (x86 only: the A9 has none and an arm64 host rejects -mfma).
set(DELUGE_TEST_UNSAFE_MATH_OPTIONS -O2 -funsafe-math-optimizations)
set(DELUGE_TEST_FMA_OPTIONS -mfma)
deluge_test_library_variant(deluge_dsp_fixed unsafe_math OPTIONS ${DELUGE_TEST_UNSAFE_MATH_OPTIONS})
deluge_test_library_variant(deluge_dsp_float unsafe_math OPTIONS ${DELUGE_TEST_UNSAFE_MATH_OPTIONS})
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
  deluge_test_library_variant(deluge_dsp_float fma OPTIONS ${DELUGE_TEST_FMA_OPTIONS})
endif()
