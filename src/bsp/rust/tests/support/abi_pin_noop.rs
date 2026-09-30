//! No-op `abi_pin!`/`abi_const!` for the host test crates. These `#[path]`-include BSP modules that end in ABI
//! pins, but have no bindgen output to pin against; `src/main.rs` and `harness/golden_vt_render` do the checking.
//! Include it with `#[macro_use]`, before the first pinned module.
#![allow(unused_macros)]

macro_rules! abi_pin {
    ($($t:tt)*) => {};
}

macro_rules! abi_const {
    ($($t:tt)*) => {};
}
