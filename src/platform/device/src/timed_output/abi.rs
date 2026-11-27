//! `libdeluge/timed_output.h`'s functions, on the device's or the host's service.

#[cfg(target_os = "none")]
use super::device::service;
#[cfg(not(target_os = "none"))]
use super::host::service;

use super::core;
use crate::sys::{DelugeMidiPort, DelugeOutStats};

/// `midi_io.h`'s DIN port.
const DIN_PORT: DelugeMidiPort = 0;
/// The USB peripheral port: the single connection to a computer.
const USB_PERIPHERAL_PORT: DelugeMidiPort = 1;

/// The service's queue for `port`, or `None` for a port with nothing behind it.
fn queue(port: DelugeMidiPort) -> Option<usize> {
    match port {
        DIN_PORT => Some(core::DIN),
        USB_PERIPHERAL_PORT if usb_connected() => Some(core::USB),
        _ => None,
    }
}

#[cfg(target_os = "none")]
fn usb_connected() -> bool {
    usb_classes::midi::connected()
}

/// The host has no USB-MIDI: `deluge_midi_usb_peripheral_connected` reads false there.
#[cfg(not(target_os = "none"))]
fn usb_connected() -> bool {
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_out_midi_at(
    port: DelugeMidiPort,
    bytes: *const u8,
    len: u32,
    stamp: u32,
) -> bool {
    let Some(q) = queue(port) else {
        return false;
    };
    if bytes.is_null() || len == 0 {
        return false;
    }
    // SAFETY: the app passes `len` valid bytes.
    let bytes = unsafe { ::core::slice::from_raw_parts(bytes, len as usize) };
    service(|s| s.push_midi(q, bytes, stamp))
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_out_midi_space(port: DelugeMidiPort) -> u32 {
    queue(port).map_or(0, |q| service(|s| s.midi_space(q)) as u32)
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_out_cv_at(channel: u8, value: u16, stamp: u32) -> bool {
    service(|s| s.push_cv(channel as usize, value, stamp))
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_out_gate_at(channel: u8, level: bool, stamp: u32) -> bool {
    service(|s| s.push_gate(channel as usize, level, stamp))
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_out_gate_cancel(mask: u8) {
    service(|s| s.cancel_gates(mask));
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_out_stats(out: *mut DelugeOutStats) {
    if out.is_null() {
        return;
    }
    let stats = service(|s| s.stats());
    // SAFETY: the app passes a valid out-pointer.
    unsafe {
        *out = DelugeOutStats {
            late: stats.late,
            dropped: stats.dropped,
            cv_late_display: 0,
            display_deferred: 0,
            display_forced: 0,
        };
    }
}

abi_pin!(
    deluge_out_cv_at,
    deluge_out_gate_at,
    deluge_out_gate_cancel,
    deluge_out_midi_at,
    deluge_out_midi_space,
    deluge_out_stats,
);
