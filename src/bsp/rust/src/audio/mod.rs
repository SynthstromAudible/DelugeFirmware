//! Audio I/O: the device's SSI DMA rings, the host render pump, and capture timing.

/// Capture stamps: where on the TX play timeline an input event happened. See its module doc.
pub mod capture_stamp;
/// audio_io.h — duplex block audio over the SSI0 DMA rings.
#[cfg(target_os = "none")]
pub(crate) mod device;
/// audio_io.h — host null-sink render pump: the priority-0 task actually
/// calls `deluge_app_render` and discards the output, instead of the no-op
/// stub in `host_link_stubs.rs`.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
pub mod host;
/// The drive's render-window pacing, which `audio` runs.
pub mod pacing;
