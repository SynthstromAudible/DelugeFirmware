# This tree builds no dsp-next library (deluge_dsp_fixed), so the specs compile the filter set the effects' filter runs,
# and the noise generator its ladders read, themselves; likewise the compressor, with the table interpolation and the
# tanh table its saturation reads; the duck envelope, with the decay tables its curves read; and the delay's tape and
# processor, which the stutter's tape shape reaches through the delay.
set(_dsp_fixed ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/dsp-next/fixed)
set(_dsp_filter ${_dsp_fixed}/filter)
# NEON: the sidechain's duck envelope runs through Control, whose kernel core includes argon.
deluge_specs_driver(effects_specs NEON
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks/mock_panic.cpp
          ${_dsp_filter}/filter_set.cpp
          ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/util/synth_noise.cpp
          ${_dsp_fixed}/dynamics/compressor_kernel.cpp
          ${_dsp_fixed}/dynamics/rms_feedback_compressor.cpp
          ${_dsp_fixed}/dynamics/duck_envelope.cpp
          ${_dsp_fixed}/interpolation/table_interpolation.cpp
          ${_dsp_fixed}/delay/delay_tape.cpp
          ${_dsp_fixed}/delay/delay_processor.cpp
          ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/util/lookuptables/lookuptables.cpp
  LIBRARIES deluge_effects
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks ${CMAKE_CURRENT_LIST_DIR}/../../dsp)
