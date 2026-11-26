//! Audio I/O: the device's SSI DMA rings, the host render pump, the render driver, and capture
//! timing.

pub mod block_drive;
pub mod capture_stamp;
#[cfg(target_os = "none")]
pub(crate) mod device;
#[cfg(feature = "host_app")]
pub mod host;
pub(crate) mod ports;
pub mod render;
pub(crate) mod ring;
pub mod timing_probe;
pub mod usb_service_probe;
