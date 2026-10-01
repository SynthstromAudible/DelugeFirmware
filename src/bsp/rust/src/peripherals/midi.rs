//! `libdeluge/midi_io.h` — DIN MIDI over `deluge_bsp::uart` and USB-MIDI as a peripheral.
//!
//! Port 0 is DIN; port 1 is the USB peripheral connection (the Deluge as a device on a computer);
//! ports 2 and up (USB-host device slots) always report disconnected, since USB host mode is not
//! implemented.
//!
//! The USB port carries whole 4-byte USB-MIDI 1.0 event packets, SysEx and cable number included:
//! the USB class runs in packet mode (selected in [`usb::usb_task`](super::usb::usb_task)) and packets
//! pass between its queues and the app unchanged, via [`midi_packets`](super::midi_packets).
//!
//! Arrival stamps (DIN and USB) are taken when the app reads the byte, not at hardware capture:
//! fine for note input, not sample-accurate for MIDI clock in.
#![allow(non_upper_case_globals)]

use deluge_bsp::uart;
use usb_classes::midi as usb_midi;

use super::midi_packets::{PACKET_BYTES, read_packets, write_packets};
use crate::sys::{
    DelugeMidiPort, DelugeUsbHostEvent, DelugeUsbHostEvent_DELUGE_USB_HOST_NONE as USB_HOST_NONE,
};

const DIN_PORT: u8 = 0;
const USB_PORT_BASE: u8 = 1;
/// `MAX_NUM_USB_MIDI_DEVICES` from `board_config.h`.
const MAX_USB_MIDI_DEVICES: u8 = 6;
const PORT_COUNT: u8 = USB_PORT_BASE + MAX_USB_MIDI_DEVICES;
/// The USB-MIDI peripheral port: the single connection to a computer.
const USB_PERIPHERAL_PORT: u8 = USB_PORT_BASE;

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_init() {
    // The runtime brings up the DIN UART and the USB device.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_port_count() -> u8 {
    PORT_COUNT
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_port(_controller: u8, device: u8) -> DelugeMidiPort {
    USB_PORT_BASE + device
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_read_timed(
    port: DelugeMidiPort,
    dst: *mut u8,
    max: u32,
    arrival_ticks: *mut u32,
) -> u32 {
    // DIN is read per byte via deluge_midi_din_read_timed(); only the USB peripheral port
    // delivers batches here.
    if port != USB_PERIPHERAL_PORT || !usb_midi::connected() || dst.is_null() {
        return 0;
    }
    // SAFETY: the app provides `max` writable bytes at `dst`.
    let dst = unsafe { core::slice::from_raw_parts_mut(dst, max as usize) };
    let written = read_packets(dst, usb_midi::try_recv_packet_from_host);
    if written > 0 && !arrival_ticks.is_null() {
        // SAFETY: caller passes a valid out-pointer when non-null.
        unsafe {
            *arrival_ticks =
                crate::audio::capture_stamp::now_for(embassy_time::Instant::now().as_ticks())
        };
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
        // Whole packets only; a full queue reports the bytes accepted so far.
        return write_packets(data, usb_midi::try_send_packet_to_host) as u32;
    }
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_space(port: DelugeMidiPort) -> u32 {
    match port {
        DIN_PORT => uart::midi_write_space() as u32,
        USB_PERIPHERAL_PORT => (usb_midi::tx_packet_free() * PACKET_BYTES) as u32,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_write_pending(port: DelugeMidiPort) -> u32 {
    match port {
        // The DIN FIFO drains on its own (TXI-driven).
        USB_PERIPHERAL_PORT => (usb_midi::tx_packet_pending() * PACKET_BYTES) as u32,
        _ => 0,
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_din_read_timed(byte: *mut u8, arrival_stamp: *mut u32) -> bool {
    match uart::try_read_midi() {
        Some(b) => {
            // SAFETY: the app passes valid out-pointers (arrival_stamp may be null).
            unsafe {
                *byte = b;
                if !arrival_stamp.is_null() {
                    *arrival_stamp = crate::audio::capture_stamp::now_for(
                        embassy_time::Instant::now().as_ticks(),
                    );
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
    // The USB device and endpoint pumps run as their own tasks (see `usb`).
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_usb_is_host() -> bool {
    // USB host mode is not implemented.
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

abi_pin!(
    deluge_midi_din_read_timed,
    deluge_midi_flush,
    deluge_midi_init,
    deluge_midi_poll_usb_host_event,
    deluge_midi_port_count,
    deluge_midi_read_timed,
    deluge_midi_service,
    deluge_midi_usb_is_host,
    deluge_midi_usb_peripheral_connected,
    deluge_midi_usb_port,
    deluge_midi_write,
    deluge_midi_write_pending,
    deluge_midi_write_space,
);
