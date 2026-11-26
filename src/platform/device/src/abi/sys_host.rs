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

/// `libdeluge/encoder_io.h`'s `DelugeEncoderEdges`: 16 bytes, seven pad bytes after `edges`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DelugeEncoderEdges {
    pub time_us: u64,
    pub edges: i8,
}

/// `libdeluge/encoder_io.h`'s `DELUGE_ENCODER_EDGE_RUNS`.
pub const DELUGE_ENCODER_EDGE_RUNS: u32 = 17;

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

/// `libdeluge/types.h`'s `DelugeStereoSample`.
#[repr(C)]
#[derive(Debug, Default, Copy, Clone, PartialEq, Eq)]
pub struct DelugeStereoSample {
    pub l: i32,
    pub r: i32,
}

/// `libdeluge/audio_ports.h`'s `DelugeAudioPortFlags`: `enum DelugeAudioPortFlags : uint8_t`.
pub type DelugeAudioPortFlags = u8;
pub const DelugeAudioPortFlags_DELUGE_PORT_MAIN_MIX_OUT: DelugeAudioPortFlags = 1;
pub const DelugeAudioPortFlags_DELUGE_PORT_MONITORABLE: DelugeAudioPortFlags = 2;
pub const DelugeAudioPortFlags_DELUGE_PORT_CODEC_INPUT: DelugeAudioPortFlags = 4;
/// `libdeluge/audio_ports.h`'s `DelugeAudioPortIndex`: `enum DelugeAudioPortIndex : uint8_t`.
pub type DelugeAudioPortIndex = u8;
pub const DelugeAudioPortIndex_DELUGE_PORT_CODEC: DelugeAudioPortIndex = 0;
pub const DelugeAudioPortIndex_DELUGE_PORT_USB: DelugeAudioPortIndex = 1;
pub const DelugeAudioPortIndex_DELUGE_PORT_COUNT: DelugeAudioPortIndex = 2;

/// The sample and port types the port structs below are made of: these stand-ins in the plain host build, and `sys`'s
/// own where the pins compare the stand-ins field for field (each is pinned by its own `abi_layout!`).
#[cfg(not(feature = "host_app"))]
type PortSample = DelugeStereoSample;
#[cfg(feature = "host_app")]
type PortSample = crate::sys::DelugeStereoSample;
#[cfg(not(feature = "host_app"))]
type BlockPort = DelugeAudioPort;
#[cfg(feature = "host_app")]
type BlockPort = crate::sys::DelugeAudioPort;

/// `libdeluge/audio_ports.h`'s `DelugeAudioPort`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct DelugeAudioPort {
    pub in_: *const *const PortSample,
    pub out: *const *mut PortSample,
    pub in_pairs: u8,
    pub out_pairs: u8,
    pub flags: u8,
    pub far_receiving: bool,
    pub far_sending: bool,
    pub out_silent: u32,
}

/// `libdeluge/audio_ports.h`'s `DelugeAudioBlock`.
#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct DelugeAudioBlock {
    pub frames: u32,
    pub ports: [BlockPort; 2],
    pub platform_cycles: u32,
}

/// `libdeluge/types.h`'s `DelugeChunkBacking`, which `deluge_storage_sample_fill` defines (as the bindgen
/// `sys` re-exports it; see `build/crate_types.rs`).
#[allow(unused_imports)]
pub use deluge_storage_sample_fill::chunk::DelugeChunkBacking;

unsafe extern "C" {
    /// `libdeluge/streaming_loader.h`: returns the resource-manager singleton (`DelugeResource` is
    /// opaque, so it binds as `c_void`).
    pub fn deluge_streaming_resource_manager() -> *mut core::ffi::c_void;
}

abi_pin!(deluge_streaming_resource_manager);
abi_layout!(type self::DelugeStatus => sys DelugeStatus);
abi_layout!(type self::DelugeInputEventKind => sys DelugeInputEventKind);
abi_layout!(self::DelugeInputEvent => sys DelugeInputEvent { kind, x, y, value });
abi_layout!(self::DelugeColour => sys DelugeColour { r, g, b });
abi_layout!(self::DelugeEncoderEdges => sys DelugeEncoderEdges { time_us, edges });
abi_layout!(self::DelugeBootInfo => sys DelugeBootInfo { pic_firmware_version, oled_present,
    factory_reset_requested });
abi_layout!(type self::DelugeAudioPortFlags => sys DelugeAudioPortFlags);
abi_layout!(type self::DelugeAudioPortIndex => sys DelugeAudioPortIndex);
abi_layout!(self::DelugeStereoSample => sys DelugeStereoSample { l, r });
abi_layout!(self::DelugeAudioPort => sys DelugeAudioPort { in_, out, in_pairs, out_pairs, flags, far_receiving,
    far_sending, out_silent });
abi_layout!(self::DelugeAudioBlock => sys DelugeAudioBlock { frames, ports, platform_cycles });
abi_const!(
    self::DELUGE_ENCODER_EDGE_RUNS => DELUGE_ENCODER_EDGE_RUNS,
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
    self::DelugeAudioPortFlags_DELUGE_PORT_MAIN_MIX_OUT => DelugeAudioPortFlags_DELUGE_PORT_MAIN_MIX_OUT,
    self::DelugeAudioPortFlags_DELUGE_PORT_MONITORABLE => DelugeAudioPortFlags_DELUGE_PORT_MONITORABLE,
    self::DelugeAudioPortFlags_DELUGE_PORT_CODEC_INPUT => DelugeAudioPortFlags_DELUGE_PORT_CODEC_INPUT,
    self::DelugeAudioPortIndex_DELUGE_PORT_CODEC => DelugeAudioPortIndex_DELUGE_PORT_CODEC,
    self::DelugeAudioPortIndex_DELUGE_PORT_USB => DelugeAudioPortIndex_DELUGE_PORT_USB,
    self::DelugeAudioPortIndex_DELUGE_PORT_COUNT => DelugeAudioPortIndex_DELUGE_PORT_COUNT,
);
