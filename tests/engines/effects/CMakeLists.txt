# This tree builds no dsp-next library (deluge_dsp_fixed), so the specs compile the filter set the effects' filter runs,
# and the noise generator its ladders read, themselves.
set(_dsp_filter ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/dsp-next/fixed/filter)
deluge_specs_driver(effects_specs
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks/mock_panic.cpp
          ${_dsp_filter}/filter_set.cpp
          ${_dsp_filter}/low_pass_ladder.cpp
          ${_dsp_filter}/high_pass_ladder.cpp
          ${_dsp_filter}/state_variable_filter.cpp
          ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge/util/waves.cpp
  LIBRARIES deluge_effects
  INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../support/mocks)
