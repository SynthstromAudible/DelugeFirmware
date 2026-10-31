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
# dsp-next/core/q31_ops.h's inline arithmetic.
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
# The noise spec draws the app's one stream, which synth_noise.cpp defines.
set(noise_spec_DEPS ${_repo_root}/src/deluge/util/synth_noise.cpp)
# The stereo spec draws from the shared noise stream, which synth_noise.cpp defines.
set(stereo_spec_DEPS ${_repo_root}/src/deluge/util/synth_noise.cpp)
# The stateful-oscillator spec drives the band-limited saw + square bands (BandLimitedTableOscillator / pulse).
set(stateful_oscillator_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/saw.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/square.cpp)
# SyncedOscillator + SyncWindower seam crossfades + CrudeSine read sine_table, which reads the sineWaveSmall LUT.
set(synced_oscillator_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
set(sync_windower_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
set(crude_oscillators_spec_DEPS ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The L1 VirtualAnalogSource composes every band-limited wave table + the sine band (sineWaveSmall).
set(supersaw_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/osc/supersaw.cpp
  ${_repo_root}/src/synth/sources/supersaw/supersaw_source.cpp)
set(virtual_analog_source_spec_DEPS
  ${_repo_root}/src/synth/sources/va/virtual_analog_source.cpp
  ${_repo_root}/src/synth/sources/va/va_shapes.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/saw.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/square.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/triangle.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/analog_saw.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/bandlimited/analog_square.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The LFO (a leaf component now): the synth's noise stream (synth_noise.cpp) + the sine/triangle LUTs.
set(lfo_spec_DEPS
  ${_repo_root}/src/synth/modulation/lfo.cpp
  ${_repo_root}/src/deluge/util/synth_noise.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The Envelope (a leaf component now): its curve helpers (dsp-next table_interpolation) + the decay/release tables.
set(envelope_spec_DEPS
  ${_repo_root}/src/synth/modulation/envelope.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The routing spec compiles the library's cable routing.
set(patch_routing_spec_DEPS ${_repo_root}/src/synth/modulation/patch_routing.cpp)
# synth::Voice played on the host: the synth library's sources, the dsp-next they render with, and the param math,
# lookup tables and noise stream it reads. Nothing of the app.
file(GLOB_RECURSE _synth_srcs CONFIGURE_DEPENDS ${_repo_root}/src/synth/*.cpp)
file(GLOB_RECURSE _dsp_fixed_srcs CONFIGURE_DEPENDS ${_repo_root}/src/deluge/dsp-next/fixed/*.cpp)
set(synth_voice_spec_DEPS
  ${_synth_srcs}
  ${_dsp_fixed_srcs}
  ${_repo_root}/src/deluge/util/param_functions.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp
  ${_repo_root}/src/deluge/util/synth_noise.cpp
  ${_repo_root}/src/deluge/util/phase_increment_fine_tuner.cpp
  ${_repo_root}/src/effects/saturation.cpp
  ${_repo_root}/tests/support/mocks/mock_panic.cpp)
# Today's patching and its frozen reference, on random cases: the same closure as the voice spec.
set(patcher_spec_DEPS ${synth_voice_spec_DEPS} ${CMAKE_CURRENT_LIST_DIR}/reference_patcher.cpp)
# The engines played on the host, through the voice and on their own: the same closure as the voice spec.
set(synth_engine_spec_DEPS ${synth_voice_spec_DEPS})
# The shared windowed-sinc kernel table + its select()/variant() variant picks.
set(windowed_sinc_kernel_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/windowed_sinc_kernel.cpp)
# The chunker caches each chunk's sinc kernel via windowed_sinc_kernel.select().
set(wavetable_cycle_chunker_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/windowed_sinc_kernel.cpp)

# The shift tracker needs no sources: the kernel is header-only, and its noise inputs come from SeededNoise.
set(shift_tracker_spec_DEPS)
# The dry/wet blend needs no sources: the kernel is header-only, and its noise inputs come from SeededNoise.
set(dry_wet_spec_DEPS)
# The filters are header-only kernels: their specs build the noise stream the low-pass ladder draws (the synth_noise
# generator in synth_noise.cpp) and the table lookups the ladders' coefficients and the verbatim references read.
set(_filter_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp
  ${_repo_root}/src/deluge/util/synth_noise.cpp)
set(low_pass_ladder_spec_DEPS ${_filter_DEPS})
set(high_pass_ladder_spec_DEPS ${_filter_DEPS})
set(svf_spec_DEPS ${_filter_DEPS})
set(fade_in_spec_DEPS ${_filter_DEPS})
# The filter set runs the three filters.
set(filter_set_spec_DEPS ${_filter_DEPS} ${_repo_root}/src/deluge/dsp-next/fixed/filter/filter_set.cpp)

# The filters' verbatim references against the firmware's filters over the recorded workloads, and what both render
# pinned (reference_filters.h, filter_workloads.h and filter_workload_records.h, header-only): the set.
set(filters_reference_spec_DEPS
  ${_filter_DEPS}
  ${_repo_root}/src/deluge/dsp-next/fixed/filter/filter_set.cpp)
# The compressor's references against the firmware's compressor over its recorded workloads, and what they render
# pinned (reference_compressor.h, compressor_workloads.h and compressor_workload_records.h, header-only): the
# compressor, with its tanh table's interpolator.
set(compressor_reference_spec_DEPS
  ${_filter_DEPS}
  ${_repo_root}/src/deluge/dsp-next/fixed/dynamics/rms_feedback_compressor.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/dynamics/compressor_kernel.cpp)
# The Compressor kernel against the fixed reference over the same workloads: the same sources.
set(compressor_kernel_spec_DEPS ${compressor_reference_spec_DEPS})

# The mod FX reference (reference_mod_fx.h, mod_fx_workloads.h, header-only) reads the sine table and draws the
# warbler's noise from synth_noise.
set(mod_fx_reference_spec_DEPS
  ${_repo_root}/src/deluge/util/synth_noise.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
set(mod_fx_kernel_spec_DEPS ${mod_fx_reference_spec_DEPS})

# The granular reference (reference_granular.h, granular_workloads.h, header-only) draws its grains from synth_noise; its
# quick_log, pan and wet low-pass are header-only.
set(granular_reference_spec_DEPS ${_repo_root}/src/deluge/util/synth_noise.cpp)
# The Granular kernel against the reference over the same workloads: the same sources, the kernel being header-only.
set(granular_kernel_spec_DEPS ${granular_reference_spec_DEPS})

# The decay-curve lookups read the decay tables.
set(table_interpolation_spec_DEPS
  ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
  ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp)
# The compressor against its exact values and against the fixed reference over the recorded workloads: the same
# sources as the reference spec.
set(rms_feedback_compressor_spec_DEPS ${compressor_reference_spec_DEPS})
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
# The delay over storage it hasn't written, through the recorded workloads: the same sources.
set(delay_poisoned_storage_spec_DEPS ${delay_processor_spec_DEPS})
# The stutter reference (reference_stutter.h, stutter_workloads.h, header-only) over the delay's tape: the same sources.
set(reference_stutter_spec_DEPS ${delay_processor_spec_DEPS})
# The Stutter kernel against the reference over the same workloads: the same sources, the kernel being header-only.
set(stutter_kernel_spec_DEPS ${reference_stutter_spec_DEPS})
# The DX7 reference (reference_dx7.h, dx7_workloads.h, dx7_workload_records.h, header-only) draws its noise from
# SeededNoise and takes the init voice from instruments/deluge.
set(dx7_reference_spec_DEPS)
# The DX7 components: the tables' generator, the envelope's advance and the graph are out of line; the rest is in
# headers.
set(_dx7_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/dx7/tables.cpp)
set(dx7_tables_spec_DEPS ${_dx7_DEPS})
set(dx7_pitch_envelope_spec_DEPS ${_dx7_DEPS})
set(dx7_lfo_delay_spec_DEPS ${_dx7_DEPS})
set(dx7_lfo_spec_DEPS ${_dx7_DEPS})
set(dx7_envelope_spec_DEPS ${_dx7_DEPS} ${_repo_root}/src/deluge/dsp-next/fixed/dx7/envelope.cpp)
set(dx7_operator_spec_DEPS ${_dx7_DEPS})
set(dx7_mki_feedback_stack_spec_DEPS ${_dx7_DEPS})
set(dx7_graph_spec_DEPS ${_dx7_DEPS} ${_repo_root}/src/deluge/dsp-next/fixed/dx7/graph.cpp)
set(dx7_graph_unsafe_math_spec_DEPS ${dx7_graph_spec_DEPS})
set(dx7_graph_unsafe_math_spec_OPTIONS -funsafe-math-optimizations)
set(dx7_modern_vector_operator_spec_DEPS ${_dx7_DEPS})
# The vector operator must equal the device's NEON kernel under the host's float flags and the device's. Only the
# device builds with -funsafe-math-optimizations, so the same spec builds twice: plain, and with the device's flag.
set(dx7_modern_vector_operator_unsafe_math_spec_DEPS ${dx7_modern_vector_operator_spec_DEPS})
set(dx7_modern_vector_operator_unsafe_math_spec_OPTIONS -funsafe-math-optimizations)
set(dx7_voice_spec_DEPS ${_dx7_DEPS} ${dx7_graph_spec_DEPS} ${dx7_envelope_spec_DEPS})
# The DX7 asm pin runs the device's NEON FM kernel (a test-local copy, reference_neon_fm_kernel.s) and exists on ARM
# only; its records are what dx7_modern_vector_operator_spec must equal on both architectures.
set(_arm_only_specs dx7_asm_pin_spec)
set(dx7_asm_pin_spec_DEPS ${CMAKE_CURRENT_LIST_DIR}/reference_neon_fm_kernel.s)
# On ARM the vector operator's specs run the asm beside it, sample by sample, and the graph spec builds the graph as the
# firmware does, its modern ops through the asm (under the test-local name), against the reference taking the asm.
if(DSP_SPEC_ARM)
  foreach(_asm_spec dx7_modern_vector_operator_spec dx7_modern_vector_operator_unsafe_math_spec dx7_graph_spec
                   dx7_graph_unsafe_math_spec)
    list(APPEND ${_asm_spec}_DEPS ${CMAKE_CURRENT_LIST_DIR}/reference_neon_fm_kernel.s)
    set(${_asm_spec}_DEFS DELUGE_DX7_ASM_PIN=1)
  endforeach()
  list(APPEND dx7_graph_spec_DEFS DELUGE_DX7_NEON_KERNEL=reference_neon_fm_kernel)
  list(APPEND dx7_graph_unsafe_math_spec_DEFS DELUGE_DX7_NEON_KERNEL=reference_neon_fm_kernel)
endif()

# ETL for the synth headers that reach util/etl_string.h (modulation/sync.h, through the LFO). The x86 unit tree links it
# everywhere; the ARM tree does not, so the specs link it themselves.
FetchContent_MakeAvailable(etl)

# fixed::fft: the twiddle tables, plain and under the firmware's float flags, and the transform.
set(_fft_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/fft/twiddles.cpp)
set(twiddles_spec_DEPS ${_fft_DEPS})
set(twiddles_spec_DEFS DELUGE_REPO_ROOT="${_repo_root}")
set(twiddles_shipping_spec_DEPS ${_fft_DEPS})
set(twiddles_shipping_spec_DEFS DELUGE_REPO_ROOT="${_repo_root}")
set(twiddles_shipping_spec_OPTIONS -O2 -funsafe-math-optimizations)
set(real_fft_spec_DEPS ${_fft_DEPS} ${_repo_root}/src/deluge/dsp-next/fixed/fft/real_fft.cpp)
set(real_fft_spec_DEFS DELUGE_REPO_ROOT="${_repo_root}")
set(real_fft_shipping_spec_DEPS ${real_fft_spec_DEPS})
set(real_fft_shipping_spec_DEFS DELUGE_REPO_ROOT="${_repo_root}")
set(real_fft_shipping_spec_OPTIONS -O2 -funsafe-math-optimizations)
# The level detector.
set(level_detector_spec_DEPS ${_repo_root}/src/deluge/dsp-next/float/dynamics/level_detector.cpp)
# The FFT bin magnitude.
set(fft_magnitude_spec_DEPS ${_repo_root}/src/deluge/dsp-next/fixed/fft/magnitude.cpp)
# The band builder: the transform, the DFT and the bin magnitude.
set(band_builder_spec_DEPS
  ${real_fft_spec_DEPS}
  ${_repo_root}/src/deluge/dsp-next/fixed/fft/real_dft.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/wavetable/band_builder.cpp
  ${_repo_root}/src/deluge/dsp-next/fixed/fft/magnitude.cpp)
set(band_builder_spec_DEFS DELUGE_REPO_ROOT="${_repo_root}")
set(band_builder_spec_INCLUDES ${_repo_root}/include)

# duck_gain is header-only.
set(duck_gain_spec_DEPS)

# The send-and-volume kernel through the chains' adapter.
set(send_and_volume_kernel_spec_DEPS ${_repo_root}/src/effects/send_and_volume.cpp)

# The EQ's shelf pair is header-only.
set(shelf_pair_spec_DEPS)

# The bitcrush's kernels through the chains' adapter.
set(bitcrush_kernels_spec_DEPS ${_repo_root}/src/effects/bitcrush.cpp)

# The click is header-only; the spec computes the menu volume as the metronome does.
set(click_spec_DEPS ${_repo_root}/src/deluge/processing/metronome/metronome.cpp)

# The input folds are header-only.
set(input_monitor_spec_DEPS)

# CONFIGURE_DEPENDS: a spec file added to an already-configured tree is built (and run) on the next build.
file(GLOB _specs CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*_spec.cpp)
foreach(_spec_path IN LISTS _specs)
  cmake_path(GET _spec_path STEM _name)
  if(NOT DSP_SPEC_ARM AND _name IN_LIST _arm_only_specs)
    continue()
  endif()

  add_executable(${_name} ${_spec_path} ${${_name}_DEPS})
  target_link_libraries(${_name} PRIVATE c++spec etl::etl)
  if(DEFINED ${_name}_DEFS)
    target_compile_definitions(${_name} PRIVATE ${${_name}_DEFS})
  endif()
  if(DEFINED ${_name}_INCLUDES)
    target_include_directories(${_name} PRIVATE ${${_name}_INCLUDES})
  endif()
  # Per-spec compile options, e.g. the device's float flags for a spec that pins float arithmetic.
  if(DEFINED ${_name}_OPTIONS)
    target_compile_options(${_name} PRIVATE ${${_name}_OPTIONS})
  endif()
  deluge_test_neon(${_name})
  target_include_directories(${_name} PRIVATE ${_repo_root}/src/deluge ${_repo_root}/src)
  set_target_properties(${_name} PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
  # Every spec checks the standard library's preconditions (span bounds among them), so that a span or container
  # read past its end fails the spec rather than going unseen at the trees' optimisation levels.
  target_compile_definitions(${_name} PRIVATE _GLIBCXX_ASSERTIONS)

  add_test(NAME ${_name} COMMAND ${_name} --verbose)
endforeach()

# Standalone-link proof for the synth core (x86 only — the closure is arch-agnostic). The synth TUs + ONLY
# their gui-free dependency TUs link into an executable with NOTHING from the app; a successful link (no
# undefined symbols, no gui/model/processing/storage objects) proves libdeluge-synth is self-contained. This
# is the link-time companion to scripts/check_synth_boundary.py's transitive include check. The dep set is the
# `nm -u` closure of the synth objects: param-math (param_functions), the numeric LUTs, interpolation, the
# noise stream (synth_noise), and the routing compile (patch_routing). The Patcher has no PatchCableSet dependency.
if(NOT DSP_SPEC_ARM)
  add_executable(synth_link_test
    ${CMAKE_CURRENT_LIST_DIR}/synth_link_main.cpp
    ${_repo_root}/src/synth/modulation/patcher.cpp
    ${_repo_root}/src/synth/modulation/envelope.cpp
    ${_repo_root}/src/synth/modulation/lfo.cpp
    ${_repo_root}/src/deluge/util/param_functions.cpp
    ${_repo_root}/src/deluge/dsp-next/fixed/interpolation/table_interpolation.cpp
    ${_repo_root}/src/deluge/util/synth_noise.cpp
    ${_repo_root}/src/deluge/util/lookuptables/lookuptables.cpp
    ${_repo_root}/src/synth/modulation/patch_routing.cpp
  )
  target_link_libraries(synth_link_test PRIVATE etl::etl)
  deluge_test_neon(synth_link_test)
  target_include_directories(synth_link_test PRIVATE ${_repo_root}/src/deluge ${_repo_root}/src ${_repo_root}/include)
  target_link_options(synth_link_test PRIVATE -Wl,--no-undefined)
  set_target_properties(synth_link_test PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
  add_test(NAME synth_link_test COMMAND synth_link_test)
endif()
