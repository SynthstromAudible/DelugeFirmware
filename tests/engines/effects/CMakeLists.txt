# This tree builds no dsp-next library (deluge_dsp_fixed), so the specs compile the filter set the effects' filter runs,
# and the noise generator its ladders read, themselves; likewise the compressor, with the table interpolation and the
# tanh table its saturation reads; and the sidechain envelope.
set(_dsp_fixed ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/dsp-next/fixed)
set(_dsp_filter ${_dsp_fixed}/filter)
deluge_specs_driver(effects_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks/mock_panic.cpp
          ${_dsp_filter}/filter_set.cpp
          ${_dsp_filter}/low_pass_ladder.cpp
          ${_dsp_filter}/high_pass_ladder.cpp
          ${_dsp_filter}/state_variable_filter.cpp
          ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/util/waves.cpp
          ${_dsp_fixed}/dynamics/rms_feedback_compressor.cpp
          ${_dsp_fixed}/dynamics/sidechain_envelope.cpp
          ${_dsp_fixed}/interpolation/table_interpolation.cpp
          ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/util/lookuptables/lookuptables.cpp
  LIBRARIES deluge_effects
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks)
