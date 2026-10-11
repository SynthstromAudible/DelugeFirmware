# dsp-next behavioural specs, registered as CTest tests.
#
# Built by BOTH test projects so the same specs run on both backends:
#   tests/unit/CMakeLists.txt     -> x86 with the SIMDe NEON backend (-m32 host)
#   tests/unit-arm/CMakeLists.txt -> ARM Cortex-A9, real NEON, run via qemu (sets DSP_SPEC_ARM)
# Every assertion is therefore also an x86-vs-NEON cross-check of the vectorised
# kernel. See tests/dsp/README.md and docs/dev/synth_phase1_l0_plan.md.

set(_repo_root ${CMAKE_CURRENT_LIST_DIR}/../..)

# CppSpec, argon and SIMDe come from the including tree's tests/support/cmake/specs.cmake, at the pins in
# cmake/deps.cmake; deluge_test_neon() gives each target argon's NEON backend, through SIMDe on a host without NEON.

# Each spec -> the dsp-next source(s) it exercises (compiled into the spec executable;
# dsp-next is header-light, so building the few sources directly is simpler than
# pulling in the whole library target here).
set(sine_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/osc/sine.cpp)
# The component framework spec needs no sources: the mixins, AsBlock and the concepts are header-only.
set(component_framework_spec_DEPS)
# The mode-dispatch spec needs no sources: ModeList and with_mode are header-only.
set(mode_spec_DEPS)
# The kernel-concept spec needs no sources: the concepts are header-only, and its reference kernels use only
# util/fixedpoint.h's inline arithmetic.
set(kernel_spec_DEPS)
# The frame and lane-pack specs need no sources: Frame and LanePack are header-only, and the kernels the lane-pack spec
# writes generically are header-only too.
set(frame_spec_DEPS)
set(lane_pack_spec_DEPS)
# The Lanes spec needs no sources: Lanes, its reference kernels and the kernels it runs against Mono are header-only.
set(lanes_spec_DEPS)
# The Mono spec needs no sources: Mono and its reference kernels are header-only.
set(mono_spec_DEPS)
# The Control spec needs no sources: Control and its reference kernels are header-only.
set(control_spec_DEPS)
# The Chain spec needs no sources: Chain, Pinned and the kernels it chains are header-only.
set(chain_spec_DEPS)
# The noise spec shares jcong, which waves.cpp defines.
set(noise_spec_DEPS ${_repo_root}/src/deluge/util/waves.cpp)
# The stereo spec draws from the shared noise stream, which waves.cpp defines.
set(stereo_spec_DEPS ${_repo_root}/src/deluge/util/waves.cpp)
# The stateful-oscillator spec drives the band-limited saw + square bands (BandLimitedTableOscillator / pulse).
set(stateful_oscillator_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/saw.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/square.cpp)
# SyncedOscillator + SyncWindower seam crossfades + CrudeSine read sine_table, which reads the sineWaveSmall LUT.
set(synced_oscillator_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
set(sync_windower_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
set(crude_oscillators_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The L1 VirtualAnalogSource composes every band-limited wave table + the sine band (sineWaveSmall).
set(virtual_analog_source_spec_DEPS
  ${_repo_root}/src/deluge/synth/source/virtual_analog_source.cpp
  ${_repo_root}/src/deluge/synth/source/va_shapes.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/saw.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/square.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/triangle.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/analog_saw.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/analog_square.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The LFO (a leaf component now): the jcong RNG state (waves.cpp) + the sine/triangle LUTs.
set(lfo_spec_DEPS
  ${_repo_root}/src/deluge/synth/lfo.cpp
  ${_repo_root}/src/deluge/util/waves.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The Envelope (a leaf component now): its curve helpers (dsp-next table_interpolation) + the decay/release tables.
set(envelope_spec_DEPS
  ${_repo_root}/src/deluge/synth/envelope.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The shared windowed-sinc kernel table + its select()/variant() variant picks.
set(windowed_sinc_kernel_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/windowed_sinc_kernel.cpp)
# The chunker caches each chunk's sinc kernel via windowed_sinc_kernel.select().
set(wavetable_cycle_chunker_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/windowed_sinc_kernel.cpp)

# The shift tracker needs no sources: the kernel is header-only, and its noise inputs come from SeededNoise.
set(shift_tracker_spec_DEPS)
# The dry/wet blend needs no sources: the kernel is header-only, and its noise inputs come from SeededNoise.
set(dry_wet_spec_DEPS)
# The filters are header-only kernels: their specs build the noise stream the low-pass ladder draws (the jcong
# generator in waves.cpp) and the table lookups the ladders' coefficients and the verbatim references read.
set(_filter_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp
  ${_repo_root}/src/deluge/util/waves.cpp)
set(low_pass_ladder_spec_DEPS ${_filter_DEPS})
set(high_pass_ladder_spec_DEPS ${_filter_DEPS})
set(svf_spec_DEPS ${_filter_DEPS})
set(fade_in_spec_DEPS ${_filter_DEPS})
# The filter set runs the three filters.
set(filter_set_spec_DEPS ${_filter_DEPS} ${_repo_root}/src/deluge/dsp-next/fixed/filter/filter_set.cpp)

# The filters' and the compressor's verbatim references against the firmware's set and compressor over the recorded
# workloads, and what both render pinned (reference_filters.h, filter_workloads.h and filter_workload_records.h,
# header-only): the set, and the compressor with its tanh table's interpolator.
set(filters_reference_spec_DEPS
  ${_filter_DEPS}
  ${_repo_root}/src/deluge/dsp-next/fixed/filter/filter_set.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/dynamics/rms_feedback_compressor.cpp)


# The decay-curve lookups read the decay tables.
set(table_interpolation_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The compressor saturates through the tanh table's bilinear interpolator.
set(rms_feedback_compressor_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/dynamics/rms_feedback_compressor.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The duck envelope's attack and release curves read the decay tables.
set(duck_envelope_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/dynamics/duck_envelope.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The delay's tape: its resampling setup and its native-rate arithmetic.
set(delay_tape_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/delay/delay_tape.cpp)
# The analog impulse response kernel and its stereo reference (src/deluge/bench) are header-only.
set(analog_impulse_response_spec_DEPS)
# The feedback shaper: the analog mode's tanh through the table interpolator.
set(feedback_shaper_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# One block of the delay: the tape, and the analog feedback's tanh through the table interpolator. The two reference
# orders, staged and frame by frame (reference_delay_processor.h), and the recorded workloads (delay_workloads.h)
# beside the spec are header-only.
set(delay_processor_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/delay/delay_processor.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/delay/delay_tape.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The delay's frame order, checked on the staged reference over the recorded workloads: the same sources.
set(delay_frame_order_spec_DEPS ${delay_processor_spec_DEPS})
# Where the delay's echoes land at the shortest delays, through both reference orders: the same sources.
set(delay_echo_spec_DEPS ${delay_processor_spec_DEPS})
# The fused Delay kernel against the frame-order reference over the recorded workloads: the same sources, the kernel
# being header-only.
set(delay_kernel_spec_DEPS ${delay_processor_spec_DEPS})

# ETL for the synth headers that reach util/etl_string.h (modulation/sync.h, through the LFO). The x86 unit tree links it
# everywhere; the ARM tree does not, so the specs link it themselves.
FetchContent_MakeAvailable(etl)

file(GLOB _specs ${CMAKE_CURRENT_LIST_DIR}/*_spec.cpp)
foreach(_spec_path IN LISTS _specs)
  cmake_path(GET _spec_path STEM _name)

  add_executable(${_name} ${_spec_path} ${${_name}_DEPS})
  target_link_libraries(${_name} PRIVATE c++spec etl::etl)
  deluge_test_neon(${_name})
  target_include_directories(${_name} PRIVATE ${_repo_root}/src/deluge ${_repo_root}/src)
  set_target_properties(${_name} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)

  add_test(NAME ${_name} COMMAND ${_name} --verbose)
endforeach()

# The filter set's spec checks the standard library's preconditions (span bounds among them), so that a scratch span
# taken past its end fails the spec rather than going unseen at the trees' optimisation levels.
target_compile_definitions(filter_set_spec PRIVATE _GLIBCXX_ASSERTIONS)

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
    ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
    ${_repo_root}/src/deluge/util/waves.cpp
    ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp
    ${_repo_root}/src/deluge/modulation/patch/patch_cable_set_amount.cpp
  )
  target_link_libraries(synth_link_test PRIVATE etl::etl)
  deluge_test_neon(synth_link_test)
  target_include_directories(synth_link_test PRIVATE ${_repo_root}/src/deluge ${_repo_root}/src ${_repo_root}/include)
  target_link_options(synth_link_test PRIVATE -Wl,--no-undefined)
  set_target_properties(synth_link_test PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
  add_test(NAME synth_link_test COMMAND synth_link_test)
endif()
