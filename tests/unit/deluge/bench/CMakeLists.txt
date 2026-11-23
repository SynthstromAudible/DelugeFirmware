# Specs for the app's DSP benchmark (src/deluge/bench). The runner and the case list are portable: they reach the
# counters only through the Counters interface, which the spec fakes, so they build from their own sources and link
# The DSP library, which the cases' components run.
set(_app ${DELUGE_REPO_ROOT}/src/deluge)
deluge_specs_driver(bench_specs TREES host arm NEON
  SOURCES
    ${_app}/bench/runner.cpp
    ${_app}/bench/cases.cpp
    ${_app}/bench/delay_feedback_cases.cpp
    ${_app}/bench/delay_cases.cpp
    ${_app}/bench/filter_cases.cpp
    ${_app}/bench/compressor_cases.cpp
    ${_app}/bench/float_filter_cases.cpp
    ${_app}/bench/float_voice_cases.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mod_fx_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/granular_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/stutter_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/pitch_shift_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/reverb_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/dx7_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/mod_matrix_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/va_shape_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fft_cases_host.cpp
    ${CMAKE_CURRENT_LIST_DIR}/chain_cases_host.cpp
  LIBRARIES deluge_dsp_fixed deluge_dsp_float deluge_test_dsp_closure
  INCLUDES ${_app} ${DELUGE_REPO_ROOT}/src)
