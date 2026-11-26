//! Umbrella staticlib for the C++ unit-test tree.
//!
//! `tests/trees/host/CMakeLists.txt` links `libdeluge_test_rust.a` into the specs. The crate re-exports the
//! member libraries so their `#[no_mangle]` C ABIs land in the archive:
//!
//! - `deluge_base_alloc` — TLSF + slab allocator (`deluge_base_alloc.h`)
//! - `deluge_storage_resource` — cached-asset / resource manager (`deluge_storage_resource.h`)
//! - `deluge_audio_format` — WAV/AIFF header decoding (`libdeluge/audio_format.h`)
//!
//! The `sample_abi` feature adds the sample crates' C ABIs, for the `sample_reader` spec.

pub use deluge_audio_format::abi::*;
pub use deluge_base_alloc::*;
pub use deluge_storage_resource::*;

// The sample crates' exports have no C++ fallback, and nothing in this crate's Rust code
// references them, so `extern crate` is what keeps their objects in the archive.
// `critical_section` supplies the std implementation behind `deluge_storage_sample_fill`'s context table.
#[cfg(feature = "sample_abi")]
extern crate deluge_storage_sample_source;

#[cfg(feature = "sample_abi")]
extern crate critical_section;
#[cfg(feature = "sample_abi")]
extern crate deluge_storage_sample_fill;

#[cfg(feature = "sample_abi")]
extern crate deluge_storage_sample_peaks;

#[cfg(feature = "sample_abi")]
extern crate deluge_storage_sample_reader;

#[cfg(feature = "sample_abi")]
extern crate deluge_storage_stream;
