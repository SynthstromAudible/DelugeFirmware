//! The timed output service behind `libdeluge/timed_output.h`: MIDI, CV and gate events sent when
//! the codec plays their stamps' frames.

#[cfg(has_app)]
mod abi;
pub mod core;
#[cfg(target_os = "none")]
pub(crate) mod device;
#[cfg(all(feature = "host_app", not(target_os = "none")))]
pub(crate) mod host;
