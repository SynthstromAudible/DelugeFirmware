# Specs for deluge::sample::SampleFrameReader (src/deluge/model/sample's reader bridge). They link the sample_abi build
# of the Rust umbrella (deluge_rust_rs_sim, defined by the host tree's root) so the bridge's deluge_sample_reader_* and
# deluge_sample_read calls resolve against the real C ABI, plus link-time stand-ins (mock_streaming_fill.cpp) for the
# app-provided accessors that ABI reaches (that file says why they are needed although this spec's scenario never runs
# them). The sim-feature reader pulls in deluge_storage_sample_fill's `app_convert` shim
# (deluge_storage_sample_convert), which needs the real deluge::audio::stream::convert_word and stitch_boundaries, so
# convert.cpp, stitch.cpp and audio_format_helpers.cpp are linked in with argon (NEON), as
# tests/unit/deluge/storage/audio/stream links them. The same driver specs the sample's transpose snapping
# (sample_transpose.cpp).
#
# Linux only: the i686 umbrella staticlib these specs link is built there alone.
if(NOT (UNIX AND NOT APPLE))
  return()
endif()
set(_app ${DELUGE_REPO_ROOT}/src/deluge)
deluge_specs_driver(sample_reader_specs TREES host NEON
  SOURCES
    mock_streaming_fill.cpp
    ${DELUGE_REPO_ROOT}/tests/support/mocks/mock_critical_section.cpp
    ${_app}/model/sample/sample_reader_bridge.cpp
    ${_app}/storage/audio/stream/convert.cpp
    ${_app}/storage/audio/stream/stitch.cpp
    ${_app}/util/audio_format_helpers.cpp
    ${_app}/model/sample/sample_transpose.cpp
  LIBRARIES
    deluge_rust_rs_sim
    pthread dl m  # the Rust std staticlib's system libraries
  INCLUDES
    ${DELUGE_REPO_ROOT}/include                       # libdeluge/file_io.h, libdeluge/streaming_loader.h
    ${DELUGE_REPO_ROOT}/src                           # definitions_cxx.hpp
    ${_app}                                           # model/sample/sample_reader_bridge.h, chunk_residency.h
    ${DELUGE_CRATE_INCLUDE_DIRS}                      # the crates' C headers (sample_peek_spec.cpp, sample_reader_bridge.h)
)
