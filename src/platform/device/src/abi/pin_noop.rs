//! No-op `abi_pin!`/`abi_const!`/`abi_layout!` for the plain host build, which has no bindgen
//! output to pin against. `pin_macros.rs` holds the checking variants.
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
