//! The plain host build's `sys`: the subset of the libdeluge headers that the modules compiled
//! there use, written by hand because that build has no bindgen output.
//!
//! The `host_app` build compiles this file again as `sys_host_check` beside the real `sys`, and
//! the pins at the bottom check every type here field for field and every constant by value.
#![allow(non_camel_case_types, non_upper_case_globals, dead_code)]

/// `libdeluge/types.h`'s `DelugeStatus`: `enum DelugeStatus : int8_t`, one byte on every target.
pub type DelugeStatus = i8;
pub const DelugeStatus_DELUGE_OK: DelugeStatus = 0;
pub const DelugeStatus_DELUGE_ERR_PARAM: DelugeStatus = -2;
pub const DelugeStatus_DELUGE_ERR_IO: DelugeStatus = -5;
pub const DelugeStatus_DELUGE_ERR_NOT_FOUND: DelugeStatus = -8;
pub const DelugeStatus_DELUGE_ERR_EXISTS: DelugeStatus = -9;
pub const DelugeStatus_DELUGE_ERR_NO_SPACE: DelugeStatus = -10;
pub const DelugeStatus_DELUGE_ERR_NO_FILESYSTEM: DelugeStatus = -11;
pub const DelugeStatus_DELUGE_ERR_NOT_EMPTY: DelugeStatus = -14;

/// `libdeluge/control_surface.h`'s `DelugeInputEventKind`: `enum DelugeInputEventKind : uint8_t`.
/// Encoder motion arrives through `deluge_encoder_take_quadrature_edges`, not as `ENCODER` events.
pub type DelugeInputEventKind = u8;
pub const DelugeInputEventKind_DELUGE_EVENT_PAD: DelugeInputEventKind = 0;
pub const DelugeInputEventKind_DELUGE_EVENT_BUTTON: DelugeInputEventKind = 1;
pub const DelugeInputEventKind_DELUGE_EVENT_ENCODER: DelugeInputEventKind = 2;
pub const DelugeInputEventKind_DELUGE_EVENT_NO_PRESSES: DelugeInputEventKind = 3;

/// `libdeluge/control_surface.h`'s `DelugeInputEvent`: 6 bytes, with one pad byte before
/// `value`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DelugeInputEvent {
    pub kind: DelugeInputEventKind, // offset 0 (1 byte)
    pub x: u8,                      // offset 1
    pub y: u8,                      // offset 2
    // 1 pad byte at offset 3
    pub value: i16, // offset 4
} // size: 6 bytes

/// `libdeluge/control_surface.h`'s `DelugeColour`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DelugeColour {
    pub r: u8,
    pub g: u8,
    pub b: u8,
}

/// `libdeluge/control_surface.h`'s `DelugeBootInfo`.
#[repr(C)]
pub struct DelugeBootInfo {
    pub pic_firmware_version: u8,
    pub oled_present: bool,
    pub factory_reset_requested: bool,
}

/// `libdeluge/types.h`'s `DelugeChunkBacking`, which `deluge_sample_fill` defines (as the bindgen
/// `sys` re-exports it; see `build/crate_types.rs`).
#[allow(unused_imports)]
pub use deluge_sample_fill::chunk::DelugeChunkBacking;

unsafe extern "C" {
    /// `libdeluge/sample_stream.h`: returns the resource-manager singleton (`DelugeResource` is
    /// opaque, so it binds as `c_void`).
    pub fn deluge_streaming_resource_manager() -> *mut core::ffi::c_void;
}

abi_pin!(deluge_streaming_resource_manager);
abi_layout!(type self::DelugeStatus => sys DelugeStatus);
abi_layout!(type self::DelugeInputEventKind => sys DelugeInputEventKind);
abi_layout!(self::DelugeInputEvent => sys DelugeInputEvent { kind, x, y, value });
abi_layout!(self::DelugeColour => sys DelugeColour { r, g, b });
abi_layout!(self::DelugeBootInfo => sys DelugeBootInfo { pic_firmware_version, oled_present,
    factory_reset_requested });
abi_const!(
    self::DelugeStatus_DELUGE_OK => DelugeStatus_DELUGE_OK,
    self::DelugeStatus_DELUGE_ERR_PARAM => DelugeStatus_DELUGE_ERR_PARAM,
    self::DelugeStatus_DELUGE_ERR_IO => DelugeStatus_DELUGE_ERR_IO,
    self::DelugeStatus_DELUGE_ERR_NOT_FOUND => DelugeStatus_DELUGE_ERR_NOT_FOUND,
    self::DelugeStatus_DELUGE_ERR_EXISTS => DelugeStatus_DELUGE_ERR_EXISTS,
    self::DelugeStatus_DELUGE_ERR_NO_SPACE => DelugeStatus_DELUGE_ERR_NO_SPACE,
    self::DelugeStatus_DELUGE_ERR_NO_FILESYSTEM => DelugeStatus_DELUGE_ERR_NO_FILESYSTEM,
    self::DelugeStatus_DELUGE_ERR_NOT_EMPTY => DelugeStatus_DELUGE_ERR_NOT_EMPTY,
    self::DelugeInputEventKind_DELUGE_EVENT_PAD => DelugeInputEventKind_DELUGE_EVENT_PAD,
    self::DelugeInputEventKind_DELUGE_EVENT_BUTTON => DelugeInputEventKind_DELUGE_EVENT_BUTTON,
    self::DelugeInputEventKind_DELUGE_EVENT_ENCODER => DelugeInputEventKind_DELUGE_EVENT_ENCODER,
    self::DelugeInputEventKind_DELUGE_EVENT_NO_PRESSES => DelugeInputEventKind_DELUGE_EVENT_NO_PRESSES,
);
