//! midi_io.h — MIDI I/O. DIN serial over deluge_bsp::uart (SCIF0, brought up in
//! main). USB-MIDI is a **peripheral** (Deluge → computer) over the deluge-bsp
//! MIDI 1.0 class on port 1; USB **host** (devices → Deluge) comes later,
//! so `usb_is_host()` is false and host-enumeration events report NONE. Port 0 =
//! DIN; port 1 = the USB peripheral connection; ports 2..=MAX (future USB-host
//! device slots) report disconnected.
//!
//! NOTE: arrival timestamps (DIN and USB) are approximated with the current
//! monotonic tick at read time (not the exact hardware capture tick) — fine for
//! note input; sample-accurate MIDI-clock-in timing is a later refinement.
#![allow(non_upper_case_globals)]

use deluge_bsp::uart;
use deluge_bsp::usb::classes::midi as usb_midi;

use crate::sys::{
    DelugeMidiPort, DelugeMidiPortKind, DelugeMidiPortKind_DELUGE_MIDI_DIN as KIND_DIN,
    DelugeMidiPortKind_DELUGE_MIDI_USB_DEVICE as KIND_USB_DEV, DelugeUsbHostEvent,
    DelugeUsbHostEvent_DELUGE_USB_HOST_NONE as USB_HOST_NONE,
};

const DIN_PORT: u8 = 0;
const USB_PORT_BASE: u8 = 1;
/// board_config.h MAX_NUM_USB_MIDI_DEVICES.
const MAX_USB_MIDI_DEVICES: u8 = 6;
const PORT_COUNT: u8 = USB_PORT_BASE + MAX_USB_MIDI_DEVICES;
/// SCIF TX FIFO depth (bytes) — back-pressure for DIN writes.
const DIN_TX_FIFO: u32 = 16;
/// USB-MIDI peripheral port: the single to-computer connection while in
/// peripheral mode (host-mode device slots 2..=MAX come later).
const USB_PERIPHERAL_PORT: u8 = USB_PORT_BASE;

/// Number of raw bytes in a complete MIDI 1.0 message, from its status byte.
/// USB-MIDI event packets always carry the status byte (no running status), so
/// this recovers the true byte count from the class's zero-padded `[u8; 3]`
/// messages before forwarding the exact bytes to the app's MIDI parser.
fn midi1_msg_len(status: u8) -> usize {
    match status & 0xF0 {
        0x80 | 0x90 | 0xA0 | 0xB0 | 0xE0 => 3, // note off/on, poly AT, CC, pitch bend
        0xC0 | 0xD0 => 2,                      // program change, channel pressure
        0xF0 => match status {
            0xF2 => 3,        // song position pointer
            0xF1 | 0xF3 => 2, // MTC quarter-frame, song select
            _ => 1,           // tune request, realtime (F6, F8..FF)
        },
        _ => 1, // defensive: a non-status leading byte — forward one byte
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_init() {
    // DIN needs no init beyond the UART main brings up; USB-MIDI is deferred.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_port_count() -> u8 {
    PORT_COUNT
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_port_kind(port: DelugeMidiPort) -> DelugeMidiPortKind {
    if port == DIN_PORT {
        KIND_DIN
    } else {
        KIND_USB_DEV
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_port(_controller: u8, device: u8) -> DelugeMidiPort {
    USB_PORT_BASE + device
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_read(port: DelugeMidiPort, dst: *mut u8, max: u32) -> u32 {
    deluge_midi_read_timed(port, dst, max, core::ptr::null_mut())
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_read_timed(
    port: DelugeMidiPort,
    dst: *mut u8,
    max: u32,
    arrival_ticks: *mut u32,
) -> u32 {
    // DIN is drained per-byte via deluge_midi_din_read_timed(); only USB ports
    // deliver batches here — today, the single peripheral connection.
    if port != USB_PERIPHERAL_PORT || !usb_midi::connected() {
        return 0;
    }
    let max = max as usize;
    let mut written = 0usize;
    // Pop whole messages only while a full 3-byte message is guaranteed to fit,
    // so a destructively-popped message is never partially dropped.
    while max - written >= 3 {
        let Some(msg) = usb_midi::try_recv_from_host() else {
            break;
        };
        let n = midi1_msg_len(msg[0]);
        // SAFETY: the app provides `max` writable bytes at `dst`; written+n <= max.
        unsafe {
            for k in 0..n {
                *dst.add(written + k) = msg[k];
            }
        }
        written += n;
    }
    if written > 0 && !arrival_ticks.is_null() {
        // SAFETY: caller passes a valid out-pointer when non-null.
        unsafe { *arrival_ticks = embassy_time::Instant::now().as_ticks() as u32 };
    }
    written as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write(port: DelugeMidiPort, src: *const u8, len: u32) -> u32 {
    // SAFETY: the app passes `len` valid bytes.
    let data = unsafe { core::slice::from_raw_parts(src, len as usize) };
    if port == DIN_PORT {
        return uart::try_write_midi(data) as u32;
    }
    if port == USB_PERIPHERAL_PORT && usb_midi::connected() {
        let mut sent = 0u32;
        for &b in data {
            if !usb_midi::try_send_to_host_byte(b) {
                break; // TX queue full — report the accepted count
            }
            sent += 1;
        }
        return sent;
    }
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_space(port: DelugeMidiPort) -> u32 {
    match port {
        DIN_PORT => DIN_TX_FIFO,
        USB_PERIPHERAL_PORT => usb_midi::tx_free() as u32,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_pending(port: DelugeMidiPort) -> u32 {
    match port {
        // DIN TXI-driven FIFO drains on its own; USB reports its queued bytes.
        USB_PERIPHERAL_PORT => usb_midi::tx_pending() as u32,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_din_read_timed(byte: *mut u8, arrival_ticks: *mut u32) -> bool {
    match uart::try_read_midi() {
        Some(b) => {
            // SAFETY: the app passes valid out-pointers (arrival_ticks may be null).
            unsafe {
                *byte = b;
                if !arrival_ticks.is_null() {
                    *arrival_ticks = embassy_time::Instant::now().as_ticks() as u32;
                }
            }
            true
        }
        None => false,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_flush(_port: DelugeMidiPort) {
    // DIN TX is a TXI-driven FIFO that drains automatically; nothing to flush.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_service() {
    // DIN has no state machine; the USB device + endpoint pumps run as their own
    // embassy tasks (see usb.rs), so there's nothing to pump here.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_port_connected(port: DelugeMidiPort) -> bool {
    match port {
        DIN_PORT => true, // DIN is always present
        USB_PERIPHERAL_PORT => usb_midi::connected(),
        _ => false, // future USB-host device slots
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_is_host() -> bool {
    // Peripheral-first: the Deluge is a USB-MIDI device. Host mode comes later.
    false
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_peripheral_connected() -> bool {
    usb_midi::connected()
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_poll_usb_host_event() -> DelugeUsbHostEvent {
    USB_HOST_NONE
}
