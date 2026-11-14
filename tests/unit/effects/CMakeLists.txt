# Specs for the effects (src/effects), built against the standalone libraries' include roots only, so a hidden
# dependency on the app fails this build. The trees build no dsp-next library (deluge_dsp_fixed), so the specs compile
# the filter set the effects' filter runs, and the noise generator its ladders read, themselves; likewise the
# compressor, with the table interpolation and the tanh table its saturation reads; the duck envelope, with the decay
# tables its curves read; and the delay's tape and processor, which the stutter's tape shape reaches through the delay.
# The dsp-next specs' workloads and references (tests/dsp) are included by name.
set(_app ${DELUGE_REPO_ROOT}/src/deluge)
set(_dsp_fixed ${_app}/dsp-next/fixed)
# NEON: the sidechain's duck envelope runs through Control, whose kernel core includes argon.
deluge_specs_driver(effects_specs TREES host arm NEON
  SOURCES ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_panic.cpp
          ${_dsp_fixed}/filter/filter_set.cpp
          ${_app}/util/synth_noise.cpp
          ${_dsp_fixed}/dynamics/compressor_kernel.cpp
          ${_dsp_fixed}/dynamics/rms_feedback_compressor.cpp
          ${_dsp_fixed}/dynamics/duck_envelope.cpp
          ${_dsp_fixed}/interpolation/table_interpolation.cpp
          ${_dsp_fixed}/delay/delay_tape.cpp
          ${_dsp_fixed}/delay/delay_processor.cpp
          ${_app}/util/lookuptables/lookuptables.cpp
  LIBRARIES deluge_effects
  INCLUDES ${DELUGE_REPO_ROOT}/tests/support/mocks ${DELUGE_REPO_ROOT}/tests/dsp)
