//! The ABI's handle rule: a handle is a `u32`, and 0 is none (`DELUGE_HANDLE_NONE`). The tables behind the handles
//! index their slots from 0, so a slot crosses the ABI as `slot + 1`.

/// `libdeluge/types.h`'s `DelugeHandle`, copied because this module compiles in carriers with no bindgen output;
/// pinned by `abi_pins.rs`.
pub type DelugeHandle = u32;

/// The handle for table slot `slot`.
pub fn to_abi(slot: u32) -> DelugeHandle {
    slot + 1
}

/// The table slot for `handle`, or `None` for `DELUGE_HANDLE_NONE`.
pub fn from_abi(handle: DelugeHandle) -> Option<u32> {
    handle.checked_sub(1)
}
