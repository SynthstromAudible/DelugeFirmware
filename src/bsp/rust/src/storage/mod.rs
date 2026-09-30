//! The SD card and the filesystem on it.

pub mod card;
pub mod efatfs;
#[cfg(feature = "host_app")]
pub mod memory_card;
pub mod sd;
#[cfg(target_os = "none")]
pub(crate) mod sd_block_device;
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
pub mod sd_image;
#[cfg(not(target_os = "none"))]
pub mod sd_image_path;
pub mod sim_block;
pub mod streaming_loader;
