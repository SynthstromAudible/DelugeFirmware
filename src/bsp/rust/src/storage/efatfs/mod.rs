//! The `embedded-fatfs` mount the storage channels read and write through.

#[cfg(target_os = "none")]
pub(crate) mod device;
#[cfg(feature = "host_app")]
pub mod host;
pub mod portable;
