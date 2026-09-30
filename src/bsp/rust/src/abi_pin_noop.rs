//! No-op `abi_pin!`/`abi_const!`/`abi_layout!` for the crates with no bindgen output: the host test crates, and
//! `src/main.rs`'s plain host build. They compile BSP modules that end in ABI pins, but have nothing to pin against;
//! `src/abi_pin_macros.rs` holds the checking variants. Include it with `#[macro_use]`, before the first pinned
//! module.
#![allow(unused_macros)]

macro_rules! abi_pin {
    ($($t:tt)*) => {};
}

macro_rules! abi_const {
    ($($t:tt)*) => {};
}

macro_rules! abi_layout {
    ($($t:tt)*) => {};
}
