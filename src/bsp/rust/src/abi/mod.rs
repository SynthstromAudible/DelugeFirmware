//! The C ABI's plumbing: the handle rule, and the build-time checks that this crate's side of
//! every header matches the header.

/// `abi_pin!`/`abi_const!`/`abi_layout!`: the checking variants where `sys` is the bindgen output, the no-ops where
/// it is `sys_host.rs`. See `abi_pin_macros.rs`.
#[cfg(any(target_os = "none", feature = "host_app"))]
#[macro_use]
mod pin_macros;
#[cfg(not(any(target_os = "none", feature = "host_app")))]
#[macro_use]
#[path = "pin_noop.rs"]
mod pin_macros;
/// The ABI's handle rule: 0 is none, and a table slot crosses as `slot + 1`. Compiled unconditionally; every
/// module that hands a table handle across the ABI converts through it.
pub mod handle;
/// Signature and value pins for what this crate's modules do not define themselves: the in-tree crates' exports,
/// the Rust side's own declarations of header functions, and the constants copied by hand.
#[cfg(any(target_os = "none", feature = "host_app"))]
pub(crate) mod pins;
