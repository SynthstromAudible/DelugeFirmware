//! The C ABI's plumbing: the handle rule, and the build-time checks that this crate's side of
//! every header matches the header.

// `abi_pin!`/`abi_const!`/`abi_layout!`: checking where `sys` is bindgen output, no-ops where it
// is `sys_host.rs`.
#[cfg(has_app)]
#[macro_use]
mod pin_macros;
#[cfg(not(has_app))]
#[macro_use]
#[path = "pin_noop.rs"]
mod pin_macros;
pub mod handle;
#[cfg(has_app)]
pub(crate) mod pins;
