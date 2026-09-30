//! Platform bring-up and the executors that run everything else.

#[cfg(target_os = "none")]
mod device;
#[cfg(target_os = "none")]
pub use device::start;

#[cfg(feature = "host-runtime")]
mod host;
#[cfg(feature = "host-runtime")]
pub use host::main;
