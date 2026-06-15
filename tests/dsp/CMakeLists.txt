# dsp-next behavioural specs, registered as CTest tests.
#
# Built by BOTH test projects so the same specs run on both backends:
#   tests/CMakeLists.txt       -> x86 with the SIMDe NEON backend (-m32 host)
#   tests/qemu/CMakeLists.txt  -> ARM Cortex-A9, real NEON, run via qemu (sets DSP_SPEC_ARM)
# Every assertion is therefore also an x86-vs-NEON cross-check of the vectorised
# kernel. See tests/dsp/README.md and docs/dev/synth_phase1_l0_plan.md.

set(_repo_root ${CMAKE_CURRENT_LIST_DIR}/../..)

# x86 needs SIMDe to supply the NEON intrinsics argon/the kernels are written against;
# the ARM cross build has a real <arm_neon.h>. Probe before fetching anything so a host
# without SIMDe simply skips these specs instead of failing the whole test configure.
if(NOT DSP_SPEC_ARM)
  find_path(SIMDE_INCLUDE_DIR simde/arm/neon.h PATHS /usr/include /usr/local/include)
  if(NOT SIMDE_INCLUDE_DIR)
    message(WARNING "SIMDe not found (install libsimde-dev) — skipping dsp-next x86 specs.")
    return()
  endif()
endif()

include(FetchContent)

# CppSpec (header-only; also brings argparse + cxx-prettyprint and the c++spec target).
FetchContent_Declare(CppSpec
  URL https://github.com/toroidal-code/cppspec/archive/refs/heads/main.tar.gz
)
FetchContent_MakeAvailable(CppSpec)

# argon: the header-only NEON/SIMDe vector wrappers the kernels use. Pinned to the
# firmware's version (keep in sync with lib/CMakeLists.txt).
FetchContent_Declare(argon
  GIT_REPOSITORY https://github.com/stellar-aria/argon
  GIT_TAG cae5738c8e4119448ea80df691c297c4c8bc2d5e
)
FetchContent_MakeAvailable(argon)

# Each spec -> the dsp-next source(s) it exercises (compiled into the spec executable;
# dsp-next is header-light, so building the few sources directly is simpler than
# pulling in the whole library target here).
set(sine_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/osc/sine.cpp)
# SyncedOscillator + SyncWindower seam crossfades call getSine(), which reads the sineWaveSmall LUT.
set(synced_oscillator_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
set(sync_windower_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The LFO (a leaf component now): the jcong RNG state (waves.cpp) + the sine/triangle LUTs.
set(lfo_spec_DEPS
  ${_repo_root}/src/deluge/synth/lfo.cpp
  ${_repo_root}/src/deluge/util/waves.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The Envelope (a leaf component now): its curve helpers (interpolation.cpp) + the decay/release tables.
set(envelope_spec_DEPS
  ${_repo_root}/src/deluge/synth/envelope.cpp
  ${_repo_root}/src/deluge/util/interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The shared windowed-sinc kernel table + its select()/variant() variant picks.
set(windowed_sinc_kernel_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/windowed_sinc_kernel.cpp)
# The chunker caches each chunk's sinc kernel via windowed_sinc_kernel.select().
set(wavetable_cycle_chunker_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/windowed_sinc_kernel.cpp)

file(GLOB _specs ${CMAKE_CURRENT_LIST_DIR}/*_spec.cpp)
foreach(_spec_path IN LISTS _specs)
  cmake_path(GET _spec_path STEM _name)

  add_executable(${_name} ${_spec_path} ${${_name}_DEPS})
  target_link_libraries(${_name} PRIVATE c++spec argon)
  target_include_directories(${_name} PRIVATE ${_repo_root}/src/deluge ${_repo_root}/src)
  set_target_properties(${_name} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)

  if(NOT DSP_SPEC_ARM)
    # Match the host-sim x86 config (sim/CMakeLists.txt): SIMDe portable NEON under -m32.
    target_compile_definitions(${_name} PRIVATE SIMDE_NO_NATIVE)
    target_compile_options(${_name} PRIVATE -msse2 -mfpmath=sse)
    target_include_directories(${_name} PRIVATE
      ${_repo_root}/sim/compat   # <arm_neon.h> -> SIMDe shim
      ${SIMDE_INCLUDE_DIR}/simde # argon's <arm/neon.h> -> SIMDe
    )
  endif()

  add_test(NAME ${_name} COMMAND ${_name} --verbose)
endforeach()

# Standalone-link proof for the synth core (x86 only — the closure is arch-agnostic). The synth TUs + ONLY
# their gui-free dependency TUs link into an executable with NOTHING from the app; a successful link (no
# undefined symbols, no gui/model/processing/storage objects) proves libdeluge-synth is self-contained. This
# is the link-time companion to scripts/check_synth_boundary.py's transitive include check. The dep set is the
# `nm -u` closure of the synth objects: param-math (param_functions), the numeric LUTs, interpolation, the
# jcong RNG (waves), and the one PatchCableSet runtime method (patch_cable_set_amount).
if(NOT DSP_SPEC_ARM)
  add_executable(synth_link_test
    ${CMAKE_CURRENT_LIST_DIR}/synth_link_main.cpp
    ${_repo_root}/src/deluge/synth/patcher.cpp
    ${_repo_root}/src/deluge/synth/envelope.cpp
    ${_repo_root}/src/deluge/synth/lfo.cpp
    ${_repo_root}/src/deluge/util/param_functions.cpp
    ${_repo_root}/src/deluge/util/interpolation.cpp
    ${_repo_root}/src/deluge/util/waves.cpp
    ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp
    ${_repo_root}/src/deluge/modulation/patch/patch_cable_set_amount.cpp
  )
  target_link_libraries(synth_link_test PRIVATE argon etl::etl)
  target_include_directories(synth_link_test PRIVATE
    ${_repo_root}/src/deluge ${_repo_root}/src ${_repo_root}/sim/compat ${SIMDE_INCLUDE_DIR}/simde)
  target_compile_definitions(synth_link_test PRIVATE SIMDE_NO_NATIVE)
  target_compile_options(synth_link_test PRIVATE -msse2 -mfpmath=sse)
  target_link_options(synth_link_test PRIVATE -Wl,--no-undefined)
  set_target_properties(synth_link_test PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
  add_test(NAME synth_link_test COMMAND synth_link_test)
endif()
