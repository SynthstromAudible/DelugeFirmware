# tests/unit/sample_reader/CMakeLists.txt
#
# deluge::sample::SampleFrameReader specs. Links the sim-feature build of the Rust
# umbrella (deluge_rust_rs_sim, defined by the parent tests/unit/CMakeLists.txt) so the bridge's
# deluge_sample_reader_*/deluge_sample_read calls resolve against the REAL C-ABI, plus trivial
# link-time stand-ins (mock_streaming_fill.cpp) for the app-provided accessors that ABI reaches
# into (see that file's own doc for why they're needed even though this spec's one scenario never
# runs them). The sim-feature reader pulls in deluge_sample_fill's `app_convert` shim (deluge_sample_
# convert), which itself needs the REAL deluge::audio::stream::convert_word/stitch_boundaries C++
# symbols -- so convert.cpp/stitch.cpp/audio_format_helpers.cpp are linked in here, with argon (NEON),
# as tests/unit/audio_stream/CMakeLists.txt does for its own convert/stitch specs.
deluge_specs_driver(sample_reader_specs NEON
  SOURCES
    mock_streaming_fill.cpp
    ../../support/mocks/mock_critical_section.cpp
    ../../../src/deluge/model/sample/sample_reader_bridge.cpp
    ../../../src/deluge/storage/audio/stream/convert.cpp
    ../../../src/deluge/storage/audio/stream/stitch.cpp
    ../../../src/deluge/util/audio_format_helpers.cpp
  LIBRARIES
    deluge_rust_rs_sim
    pthread dl m  # the Rust std staticlib's system libraries
  INCLUDES
    ${CMAKE_CURRENT_LIST_DIR}/../../../include    # libdeluge/sample_reader.h, streaming_fill.h, system.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../src        # definitions_cxx.hpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/deluge # model/sample/sample_reader_bridge.h, storage/audio/stream/chunk_residency.h
    ${CMAKE_CURRENT_LIST_DIR}/../../../crates/deluge_resource/include # deluge_resource.h (sample_peek_spec.cpp's own manager setup)
)
