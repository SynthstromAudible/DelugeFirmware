# tests/unit/audio_stream/CMakeLists.txt
#
# deluge::audio::stream ReadSource specs. Exercises the seam via an in-memory MockReadSource
# (header-only) — no card, no FatFS, no resource manager.
#
# NEON: convert.cpp's vectorised conversion loops go through argon, and testing them here is only worth it on the
# backend the golden renders through.
deluge_specs_driver(audio_stream_specs NEON
  SOURCES
    # convert_spec.cpp exercises the real convert_word, and stitch_spec.cpp the real stitch_boundaries (which calls
    # convert_word), so both translation units are linked in.
    ../../../src/deluge/storage/audio/stream/convert.cpp
    ../../../src/deluge/storage/audio/stream/stitch.cpp
    ../../../src/deluge/util/audio_format_helpers.cpp
  INCLUDES
    ../../../include    # libdeluge/types.h
    ../../../src        # definitions_cxx.hpp
    ../../../src/deluge # storage/audio/stream/read_source.h, io/stream.hpp
)
