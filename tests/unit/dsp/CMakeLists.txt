# tests/unit/dsp/CMakeLists.txt
#
# deluge::dsp render-kernel specs. Drives the real oscillator kernel on the host
# so degenerate-input crashes (divide-by-zero on a stalled phase increment) are
# caught here rather than on hardware.
#
# NEON: oscillator.cpp reaches <argon.hpp> transitively through wave_table.h -> storage/audio/stream/convert.h.
deluge_specs_driver(dsp_specs NEON
  SOURCES
    # The real kernel under test, plus the wave tables and render helpers it calls. mocks.cpp stands in for the
    # firmware globals it reads.
    ../../../src/deluge/dsp/oscillators/oscillator.cpp
    ../../../src/deluge/dsp/oscillators/basic_waves.cpp
    ../../../src/deluge/dsp/oscillators/sine_osc.cpp
    ../../../src/deluge/util/lookuptables/lookuptables.cpp
    ../../../src/deluge/util/lookuptables/analog_square.cpp
    ../../../src/deluge/util/lookuptables/saw.cpp
    ../../../src/deluge/util/lookuptables/square.cpp
    ../../../src/deluge/util/lookuptables/mystery_synth_a_saw.cpp
    ../../../src/deluge/util/lookuptables/mystery_synth_b_saw.cpp
    mocks.cpp
  INCLUDES
    ../../../include
    ../../../src
    ../../../src/deluge
)
