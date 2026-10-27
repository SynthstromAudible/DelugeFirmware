//! The board's peripherals behind the libdeluge headers.

// Also on host under `host_app`: the descriptor and probe are pure logic.
#[cfg(has_app)]
pub mod board;
pub mod control;
#[cfg(target_os = "none")]
pub(crate) mod cv_gate;
// Also on host: the latest-value store is pure, for its tests.
pub(crate) mod cv_latest;
pub mod display;
#[cfg(target_os = "none")]
pub(crate) mod fault;
pub(crate) mod flash;
#[cfg(target_os = "none")]
pub(crate) mod midi;
pub mod midi_packets;
#[cfg(target_os = "none")]
pub(crate) mod signals;
#[cfg(not(target_os = "none"))]
pub mod ui_harness;
#[cfg(target_os = "none")]
pub(crate) mod usb;
#[cfg(target_os = "none")]
pub(crate) mod usb_audio;
// Also on host: the PCM, ring and pacing logic is pure, for the host tests.
pub(crate) mod usb_audio_stream;
