//! Audio I/O: the device's SSI DMA rings, the host render pump, and capture timing.

pub mod capture_stamp;
#[cfg(target_os = "none")]
pub(crate) mod device;
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
pub mod host;
pub mod pacing;
