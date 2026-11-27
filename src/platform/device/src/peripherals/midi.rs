//! `libdeluge/midi_io.h` — MIDI input over `deluge_bsp::uart` (DIN) and USB-MIDI as a peripheral.
//! Output goes through the timed output service (`crate::timed_output`).
//!
//! Port 0 is DIN; port 1 is the USB peripheral connection (the Deluge as a device on a computer);
//! ports 2 and up (USB-host device slots) always report disconnected, since USB host mode is not
//! implemented.
//!
//! The USB port carries whole 4-byte USB-MIDI 1.0 event packets, SysEx and cable number included:
//! the USB class runs in packet mode (selected in [`usb::usb_task`](super::usb::usb_task)) and packets
//! pass from its queue to the app unchanged, via [`midi_packets`](super::midi_packets).
//!
//! Arrival stamps place each input on the play timeline where it happened: a DIN byte is stamped
//! by the receive interrupt as it arrives, a USB packet when its transfer's read completed. Each USB
//! read returns one transfer's packets, so its one stamp is exact for all of them.
#![allow(non_upper_case_globals)]

use deluge_bsp::uart;
use usb_classes::midi as usb_midi;

use core::cell::RefCell;

use critical_section::Mutex;

use super::midi_packets::TransferReader;
use crate::sys::{
    DelugeMidiPort, DelugeUsbHostEvent, DelugeUsbHostEvent_DELUGE_USB_HOST_NONE as USB_HOST_NONE,
};

const USB_PORT_BASE: u8 = 1;
/// `MAX_NUM_USB_MIDI_DEVICES` from `board_config.h`.
const MAX_USB_MIDI_DEVICES: u8 = 6;
const PORT_COUNT: u8 = USB_PORT_BASE + MAX_USB_MIDI_DEVICES;
/// The USB-MIDI peripheral port: the single connection to a computer.
const USB_PERIPHERAL_PORT: u8 = USB_PORT_BASE;

/// The USB peripheral port's reader, which returns one transfer per read.
static USB_READER: Mutex<RefCell<TransferReader>> = Mutex::new(RefCell::new(TransferReader::new()));

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
    let Some((written, ticks)) = critical_section::with(|cs| {
        USB_READER
            .borrow_ref_mut(cs)
            .read(dst, usb_midi::try_recv_stamped_packet_from_host)
    }) else {
        return 0;
    };
    if !arrival_ticks.is_null() {
        // SAFETY: caller passes a valid out-pointer when non-null.
        unsafe { *arrival_ticks = crate::audio::capture_stamp::now_for(ticks) };
    }
    written as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_midi_din_read_timed(byte: *mut u8, arrival_stamp: *mut u32) -> bool {
    match uart::try_read_midi_stamped() {
        Some((b, ticks)) => {
            // SAFETY: the app passes valid out-pointers (arrival_stamp may be null).
            unsafe {
                *byte = b;
                if !arrival_stamp.is_null() {
                    *arrival_stamp = crate::audio::capture_stamp::now_for(ticks);
                }
            }
            true
        }
        None => false,
    }
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
    deluge_midi_init,
    deluge_midi_poll_usb_host_event,
    deluge_midi_port_count,
    deluge_midi_read_timed,
    deluge_midi_service,
    deluge_midi_usb_is_host,
    deluge_midi_usb_peripheral_connected,
    deluge_midi_usb_port,
);
