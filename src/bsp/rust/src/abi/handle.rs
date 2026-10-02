//! The ABI's handle rule: a handle is a `u32`, and 0 is none (`DELUGE_HANDLE_NONE`). Tables index
//! their slots from 0, so a slot crosses the ABI as `slot + 1`. Every module that hands a table
//! handle across the ABI converts through here.

/// `libdeluge/types.h`'s `DelugeHandle`, copied because the plain host build has no bindgen
/// output; pinned against the header in `pins.rs`.
pub type DelugeHandle = u32;

/// Returns the handle for table slot `slot`.
pub fn to_abi(slot: u32) -> DelugeHandle {
    slot + 1
}

/// Returns the table slot for `handle`, or `None` for `DELUGE_HANDLE_NONE`.
pub fn from_abi(handle: DelugeHandle) -> Option<u32> {
    handle.checked_sub(1)
}
