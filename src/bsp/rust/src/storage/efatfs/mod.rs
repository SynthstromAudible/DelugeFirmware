//! The `embedded-fatfs` mount the storage channels read and write through.

/// The single-owner `embedded-fatfs` mount — one `FileSystem` behind an
/// async `Mutex`, the only way live code touches the vendored FS.
/// `mount()`/`with_fs()` are invoked from this crate's boot path and the
/// streaming read path.
#[cfg(target_os = "none")]
pub(crate) mod device;
/// Host counterpart of `efatfs_fs` — mounts `embedded-fatfs` over a
/// block device backed by the SD_BUS-guarded async helpers (see that
/// module's doc) so a host harness can measure the real efatfs read path.
/// Test infrastructure only; no device path touched.
#[cfg(feature = "host_app")]
pub mod host;
/// Storage-generic, host-testable core of the efatfs read path
/// (`HandleTable` + `FileContext` detach/reattach + generation guard + fill
/// loop). `efatfs_fs` wraps it with the device statics/mutexes/FFI; host tests
/// drive it directly. Also reachable under `host_app`
/// (any target) so `efatfs_host_shim` — the
/// host counterpart of `efatfs_fs` — can reuse it unchanged; see that module's
/// doc.
#[cfg(any(target_os = "none", feature = "host_app"))]
pub(crate) mod portable;
