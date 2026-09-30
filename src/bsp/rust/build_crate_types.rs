//! The header types the in-tree crates define in Rust. Shared by `build.rs` and
//! `harness/golden_vt_render/build.rs`.
//!
//! bindgen does not generate these. `sys` re-exports each crate's own type under the header's name
//! instead, so a crate's export and its generated declaration name the same Rust type and
//! `abi_pin!` can compare them. The header's own definition of each struct and enum is generated
//! separately, into `sys_layout`, for `abi_layout!` to check the crate's definition against.

use bindgen::Builder;

/// Structs: (header name, the Rust type `sys` binds it to). `sys_layout` keeps each one.
#[rustfmt::skip]
const STRUCTS: &[(&str, &str)] = &[
    ("DelugeOptionalF32", "deluge_audio_format::abi::DelugeOptionalF32"),
    ("DelugeAudioHeader", "deluge_audio_format::abi::DelugeAudioHeader"),
    ("DelugeAudioWindow", "deluge_audio_format::abi::DelugeAudioWindow"),
    ("DelugeAudioHeaderParser", "deluge_audio_format::abi::DelugeAudioHeaderParser"),
    ("DelugeStreamingFillContext", "deluge_sample_fill::FillContext"),
    ("StreamingFillDescriptor", "deluge_sample_fill::StreamingFillDescriptor"),
    ("DelugeFrameWindow", "deluge_sample_reader::abi::DelugeFrameWindow"),
    ("DelugeSampleGeometry", "deluge_sample_source::geometry::Geometry"),
    ("DelugeSampleRegion", "deluge_sample_source::abi::DelugeSampleRegion"),
    ("DelugeSampleStreamGeometry", "deluge_sample_stream::DelugeSampleStreamGeometry"),
];

/// Enums, likewise. `sys_layout` generates each as a Rust enum, so `abi_layout!` can match on its variants.
#[rustfmt::skip]
const ENUMS: &[(&str, &str)] = &[
    ("DelugeReadHint", "deluge_sample_reader::reader::ReadHint"),
    ("DelugeLoadMode", "deluge_sample_reader::reservation::LoadMode"),
    ("DelugeRegionState", "deluge_sample_source::abi::DelugeRegionState"),
];

/// Opaque handles, which have no layout to keep.
#[rustfmt::skip]
const OPAQUE: &[(&str, &str)] = &[
    // Several crates pass the manager through without owning it, so they all see it untyped.
    ("DelugeResource", "core::ffi::c_void"),
    ("DelugeSampleReader", "deluge_sample_reader::abi::DelugeSampleReader"),
    ("DelugeSampleReservation", "deluge_sample_reader::reservation::DelugeSampleReservation"),
    ("DelugeSampleSource", "deluge_sample_source::abi::DelugeSampleSource"),
];

/// Opaque handles from the crate only the device build depends on (as `fs_alloc`).
#[rustfmt::skip]
const DEVICE_OPAQUE: &[(&str, &str)] = &[
    ("DelugeHeap", "fs_alloc::DelugeHeap"),
    ("DelugeSlab", "fs_alloc::slab::DelugeSlab"),
];

/// Binds each crate-defined header type to its crate's type in `sys`. An enum's constants go too:
/// bindgen types them as the enum, which is now a Rust enum, and `sys_layout` keeps their values.
pub fn bind_crate_types(mut builder: Builder, device: bool) -> Builder {
    let device_only: &[(&str, &str)] = if device { DEVICE_OPAQUE } else { &[] };
    for (header, rust) in STRUCTS.iter().chain(ENUMS).chain(OPAQUE).chain(device_only) {
        builder = builder
            .blocklist_type(header)
            .blocklist_item(format!("{header}_.*"))
            .raw_line(format!(
                "#[allow(unused_imports)]\npub use {rust} as {header};"
            ));
    }
    builder
}

/// Restricts a builder to the header's own definitions of the crate-defined structs and enums, for
/// `sys_layout`.
pub fn crate_type_layouts(mut builder: Builder) -> Builder {
    for (header, _) in STRUCTS {
        builder = builder.allowlist_type(header);
    }
    for (header, _) in ENUMS {
        builder = builder.allowlist_type(header).rustified_enum(header);
    }
    builder
}
