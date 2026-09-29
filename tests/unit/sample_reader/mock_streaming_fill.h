#pragma once

// This header pulls in the C-ABI declarations (`libdeluge/streaming_fill.h`'s manager/async/read
// entry points + `storage/audio/stream/chunk_residency.h`'s `deluge_streaming_define_asset`) that
// `mock_streaming_fill.cpp` DEFINES as trivial link-time stand-ins -- real bodies normally supplied
// by the app's `chunk_residency.cpp`/`async_fill.cpp`, which this host test does NOT link.
// `sample_reader_bridge_spec.cpp` only exercises `SampleFrameReader` over an asset with no
// registered fill-context, so none of these ever actually RUN for it -- but every
// `deluge_sample_reader_*` call any spec in this suite makes pulls the whole
// `deluge_sample_reader`/`deluge_sample_fill` archive members out of `deluge_rust_rs_sim`, and
// THEIR object code references these symbols unconditionally (same translation unit as the
// functions the specs do call), so the link still needs a body for each one. Mirrors
// `deluge_sample_reader`'s own `#[cfg(test)] host_streaming_stubs` module (Rust side) and this test
// suite's sibling `mock_source.h`/`mock_read_source.h` mocks.
#include "libdeluge/streaming_fill.h"
#include "storage/audio/stream/chunk_residency.h"

/// @brief Test control: set the process-wide "active manager" that `deluge_streaming_resource_manager()`
///        resolves.
///
/// Mirrors the Rust reader crate's own `host_streaming_stubs::set_active_manager`. Defaults to
/// NULL, matching `sample_reader_bridge_spec.cpp`'s deliberately-unregistered-asset scenario;
/// `sample_peek_spec.cpp` calls this to register a REAL manager first, so `deluge_sample_peek` has
/// something to peek into.
/// @param handle The resource manager to make active, or `nullptr` to clear it.
void set_active_manager(DelugeResource* handle);
