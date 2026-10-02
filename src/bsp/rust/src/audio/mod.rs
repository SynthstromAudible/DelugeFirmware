//! Audio I/O: the device's SSI DMA rings, the host render pump, the render driver, and capture
//! timing.

pub mod capture_stamp;
#[cfg(target_os = "none")]
pub(crate) mod device;
#[cfg(feature = "host_app")]
pub mod host;
pub mod pacing;
pub mod render;
