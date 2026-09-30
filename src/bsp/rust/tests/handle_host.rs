#![cfg(not(target_os = "none"))]

#[path = "../src/abi_handle.rs"]
mod abi_handle;

use abi_handle::{from_abi, to_abi};

#[test]
fn slot_zero_is_handle_one() {
    assert_eq!(to_abi(0), 1);
    assert_eq!(from_abi(1), Some(0));
}

#[test]
fn handle_zero_is_none() {
    assert_eq!(from_abi(0), None);
}

#[test]
fn every_slot_round_trips() {
    for slot in 0..64 {
        assert_eq!(from_abi(to_abi(slot)), Some(slot));
    }
}
