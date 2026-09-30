//! The `embedded-fatfs` mount the storage channels read and write through.

#[cfg(target_os = "none")]
pub(crate) mod device;
#[cfg(feature = "host_app")]
pub mod host;
pub mod portable;

/// The mount this build links: the device's, or the host's under `host_app`.
#[cfg(target_os = "none")]
pub(crate) use device as backend;
#[cfg(feature = "host_app")]
pub(crate) use host as backend;
