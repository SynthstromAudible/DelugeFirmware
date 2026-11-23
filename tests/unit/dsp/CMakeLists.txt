# The DSP library and synth specs built with the tree's own flags: on the x86 host (NEON through SIMDe), on the A9 under
# qemu (its own NEON) and on the host under UBSan, so every assertion that holds on both architectures is also an
# x86-against-NEON check of a vector kernel. The specs held to other flags build beside them, one driver per
# directory: fused/ (an FMA target's -mfma), unsafe_math/ (the device's float flags) and arm/ (the A9's own assembly).
# README.md says how to add one.
if(DELUGE_TEST_TREE STREQUAL "host" AND NOT (UNIX AND NOT APPLE))
  # The specs assume the -m32 host build the Linux host tree is, as the host sim's DSP does. The triangle distribution
  # spec (dsp/core's header alone) assumes nothing of it, so a host tree off Linux builds that one by itself.
  deluge_specs_driver(dsp_specs ONLY_HOST triangle_distribution_spec.cpp
    INCLUDES ${DELUGE_REPO_ROOT}/src/deluge ${DELUGE_REPO_ROOT}/src)
  return()
endif()
include(${CMAKE_CURRENT_LIST_DIR}/dsp_specs.cmake)

# The libraries as the tree builds them (tests/support/cmake/libs.cmake); the effects link-only, so their warning set
# stays theirs. SOURCES: the allocator seam the synth library reaches, the frozen reference patcher and the rules model
# the matrix specs compare against, and the metronome whose menu volume click_spec computes. The triangle
# distribution spec (dsp/core's header alone) is the host tree's only.
deluge_specs_driver(dsp_specs TREES host arm ubsan NEON
  ONLY_HOST triangle_distribution_spec.cpp
  SOURCES alloc_seams.cpp reference_patcher.cpp matrix_rules_model.cpp
          ${DELUGE_REPO_ROOT}/src/deluge/processing/metronome/metronome.cpp
          ${DELUGE_DSP_SPEC_ASM}
  LIBRARIES deluge_synth $<LINK_ONLY:deluge_effects> deluge_dsp_fixed deluge_dsp_float deluge_test_dsp_closure
            etl::etl
  INCLUDES ${DELUGE_DSP_SPEC_INCLUDES})
if(TARGET dsp_specs)
  deluge_dsp_spec_flags(dsp_specs)
endif()

# Standalone-link proof for the synth core (x86 only: the closure is arch-agnostic). The synth TUs and only their
# gui-free dependency TUs link into an executable with nothing from the app; a successful link (no undefined symbols,
# no gui/model/processing/storage objects) proves libdeluge-synth is self-contained. This is the link-time companion
# to scripts/check_layers.py's transitive synth-boundary rule. The source set is the `nm -u` closure of the synth
# objects: param-math (param_functions), the numeric LUTs, interpolation, the noise stream (synth_noise), the routing
# compile (patch_routing) and the matrix (compiler, evaluator). It is listed by hand, not linked from the libraries:
# the list is what the proof states.
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "arm")
  add_executable(synth_link_test
    ${CMAKE_CURRENT_LIST_DIR}/synth_link_main.cpp
    ${DELUGE_REPO_ROOT}/src/synth/matrix/compiler.cpp
    ${DELUGE_REPO_ROOT}/src/synth/matrix/evaluator.cpp
    ${DELUGE_REPO_ROOT}/src/synth/modulation/envelope.cpp
    ${DELUGE_REPO_ROOT}/src/synth/modulation/lfo.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/param_functions.cpp
    ${DELUGE_REPO_ROOT}/src/dsp/fixed/interpolation/table_interpolation.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/synth_noise.cpp
    ${DELUGE_REPO_ROOT}/src/deluge/util/lookuptables/lookuptables.cpp
    ${DELUGE_REPO_ROOT}/src/synth/modulation/patch_routing.cpp
  )
  target_link_libraries(synth_link_test PRIVATE etl::etl)
  deluge_test_neon(synth_link_test)
  target_include_directories(synth_link_test PRIVATE
    ${DELUGE_REPO_ROOT}/src/deluge ${DELUGE_REPO_ROOT}/src ${DELUGE_REPO_ROOT}/include)
  target_link_options(synth_link_test PRIVATE -Wl,--no-undefined)
  set_target_properties(synth_link_test PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
  add_test(NAME synth_link_test COMMAND synth_link_test)
endif()
