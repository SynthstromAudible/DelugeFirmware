//! Platform bring-up and the executors that run everything else.

#[cfg(target_os = "none")]
mod device;
#[cfg(target_os = "none")]
pub use device::start;

#[cfg(not(target_os = "none"))]
mod host;
#[cfg(not(target_os = "none"))]
pub use host::main;
