# Specs for the app's DSP benchmark (src/deluge/bench), added by both the x86 (tests/unit) and ARM (tests/unit-arm)
# test trees. The runner and the case list are portable: they reach the counters only through the Counters interface,
# which the spec fakes, so they build from their own sources and the dsp-next sources the cases' components use.
set(_src ${CMAKE_CURRENT_LIST_DIR}/../../src/deluge)
deluge_specs_driver(bench_specs NEON
  SOURCES
    ${_src}/bench/runner.cpp
    ${_src}/bench/cases.cpp
    ${_src}/bench/delay_feedback_cases.cpp
    ${_src}/bench/delay_cases.cpp
    ${_src}/dsp-next/fixed/delay/delay_processor.cpp
    ${_src}/dsp-next/fixed/delay/delay_tape.cpp
    ${_src}/dsp-next/fixed/bandlimited/saw.cpp
    ${_src}/dsp-next/fixed/bandlimited/square.cpp
    ${_src}/dsp-next/fixed/dynamics/duck_envelope.cpp
    ${_src}/dsp-next/fixed/interpolation/table_interpolation.cpp
    ${_src}/dsp-next/fixed/interpolation/windowed_sinc_kernel.cpp
    ${_src}/util/lookuptables/lookuptables.cpp
  INCLUDES ${_src} ${_src}/..)
