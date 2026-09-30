//! The board's peripherals behind the libdeluge headers.

// Also on host under `host_app`: the descriptor and probe are pure logic.
#[cfg(has_app)]
pub mod board;
pub mod control;
#[cfg(target_os = "none")]
pub(crate) mod cv_gate;
pub mod display;
#[cfg(target_os = "none")]
pub(crate) mod fault;
pub(crate) mod flash;
#[cfg(target_os = "none")]
pub(crate) mod midi;
#[cfg(target_os = "none")]
pub(crate) mod signals;
#[cfg(target_os = "none")]
pub(crate) mod usb;
