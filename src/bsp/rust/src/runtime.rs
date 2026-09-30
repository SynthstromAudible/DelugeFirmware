//! Platform bring-up and the executors that run everything else.

#[cfg(target_os = "none")]
mod device;
#[cfg(target_os = "none")]
pub use device::start;

#[cfg(all(not(target_os = "none"), feature = "host-runtime"))]
mod host;
#[cfg(all(not(target_os = "none"), feature = "host-runtime"))]
pub use host::main;
