//! The SD card and the filesystem on it.

/// card.h — `deluge_card_flush`, the one ejection hook every loader on this BSP is
/// invalidated through. Gated identically to `audio_loader` above, which it calls into: the
/// symbol has to exist wherever a loader's tables do.
pub mod card;
/// The `embedded-fatfs` mount.
pub mod efatfs;
/// The emulator's card as a directory: a memory disk filled from it, written back when the emulator closes.
#[cfg(feature = "host_app")]
pub mod memory_card;
/// block_device.h — the C-ABI SD-card entry points (`deluge_block_ready`/
/// `deluge_block_read`/`deluge_block_write`) and the shared `SD_BUS`/async
/// helpers over `deluge_bsp::sd`. The app's real transfers go through
/// `fat_block_device::SdBlockDevice` (device) and
/// `efatfs_host_shim::HostSdBlockDevice` (host) instead — see `sd.rs`'s
/// module doc.
pub mod sd;
/// `block_device_driver::BlockDevice<512>` over the real SD driver
/// (`deluge_bsp::sd`), feeding the `BufStream`/`embedded-fatfs` stack —
/// device-only counterpart of `fs_differential`'s host `FileBlockDevice`.
#[cfg(target_os = "none")]
pub(crate) mod sd_block_device;
/// Streaming-underrun harness: packs a real FAT SD image from the golden
/// harness's song/sample corpus for [`scenario`] to load. `host_app`-only.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
pub mod sd_image;
/// Content-addressed naming/reuse policy for [`sd_image`]'s temp FAT images. Split out so it
/// stays plain-host testable without the `host_app` CMake build (tests/sd_image_path_host.rs).
#[cfg(not(target_os = "none"))]
pub mod sd_image_path;
/// The blocking-I/O seam: a plain non-yielding block on device, and on host a spin driven
/// by a harness-registered progress hook. See the module doc for why host needs the hook.
pub mod sim_block;
/// The async cluster-fill task and its wakeup C ABI: drains the resource
/// manager's loader queue on this executor, awaiting the SD read instead of
/// running it inline. Compiled on the Embassy BSP unconditionally (device, or
/// host under `host_app`).
pub mod streaming_loader;
