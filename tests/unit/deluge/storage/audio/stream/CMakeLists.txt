# Specs for deluge::audio::stream (src/deluge/storage/audio/stream): the ReadSource seam through an in-memory
# MockReadSource (header-only), with no card, no FatFS and no resource manager, and the conversion and stitching
# kernels. The ARM tree builds only the NEON parity spec: argon's SIMD conversion primitives against their scalar
# references on real NEON, where SIMDe's emulation diverges.
#
# NEON: convert.cpp's vectorised conversion loops go through argon, and testing them is only worth it on the backend
# the golden renders through.
set(_app ${DELUGE_REPO_ROOT}/src/deluge)
deluge_specs_driver(audio_stream_specs TREES host NEON ONLY_ARM convert_simd_neon_parity_spec.cpp
  SOURCES
    # convert_spec.cpp exercises the real convert_word, and stitch_spec.cpp the real stitch_boundaries (which calls
    # convert_word), so both translation units are linked in.
    ${_app}/storage/audio/stream/convert.cpp
    ${_app}/storage/audio/stream/stitch.cpp
    ${_app}/util/audio_format_helpers.cpp
  INCLUDES
    ${DELUGE_REPO_ROOT}/include  # libdeluge/types.h
    ${DELUGE_REPO_ROOT}/src      # definitions_cxx.hpp
    ${_app}                      # storage/audio/stream/read_source.h, io/stream.hpp
    ${DELUGE_CRATE_INCLUDE_DIRS} # the crates' C headers read_source.h includes
)
