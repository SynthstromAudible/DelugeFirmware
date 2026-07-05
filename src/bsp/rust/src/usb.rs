//! USB device (peripheral) bring-up: a USB-MIDI 1.0 device (Deluge → computer).
//!
//! Peripheral-first: the Deluge enumerates as a class-compliant USB-MIDI
//! device so a computer can send/receive MIDI. USB **host** mode (MIDI devices
//! plugged into the Deluge) comes later; here the libdeluge USB role is
//! fixed to peripheral (`deluge_midi_usb_is_host()` → false).
//!
//! The embassy-usb device + the deluge-bsp MIDI class run as background tasks
//! under the same cooperative executor as the app tick; the app reads/writes the
//! MIDI byte stream through `midi.rs` (port 1), which bridges the class's
//! `try_recv_from_host` / `try_send_to_host_byte` byte API.

use core::mem::MaybeUninit;
use core::ptr::addr_of_mut;

use deluge_bsp::usb::classes::midi::{self, MidiClassHandler};
use embassy_usb::{Builder, Config, UsbDevice};
use rza1l_hal::usb::{
    self, Rusb1Driver, Rusb1EndpointIn, Rusb1EndpointOut, dcd_int_handler, init_device_mode,
};

// 'static descriptor + control buffers for the embassy-usb builder. A MIDI-only
// device needs little config space; sized generously.
static mut CONFIG_DESC: [u8; 256] = [0; 256];
static mut BOS_DESC: [u8; 64] = [0; 64];
static mut MSOS_DESC: [u8; 0] = [];
static mut CONTROL_BUF: [u8; 64] = [0; 64];
/// 'static home for the MIDI class handler (builder.handler needs a 'static ref).
static mut MIDI_HANDLER: MaybeUninit<MidiClassHandler> = MaybeUninit::uninit();

// Development USB identity — self-contained, NOT a product ID. Replace with an
// owned VID/PID before distribution. See deluge_bsp::usb::ids.
const USB_VID: u16 = 0x16D0;
const USB_PID: u16 = 0x0EDA;

/// Built USB device + the MIDI 1.0 bulk endpoints, handed to the executor to be
/// spawned as background tasks.
pub struct UsbMidi {
    pub device: UsbDevice<'static, Rusb1Driver>,
    pub ep_out: Rusb1EndpointOut,
    pub ep_in: Rusb1EndpointIn,
}

/// Bring up USB0 in device mode and build a USB-MIDI 1.0 device. Registers the
/// USB0 GIC handler (device-mode dispatch); the driver's `Bus::enable()` (inside
/// `device.run()`) sets the priority and enables the GIC line. Call once in the
/// boot setup window, before interrupts are unmasked.
///
/// # Safety
/// Touches USB + GIC registers and the 'static USB buffers; call exactly once
/// during single-threaded startup.
pub unsafe fn build() -> UsbMidi {
    // Route USB0 → the device-mode interrupt handler (no host dispatch — host
    // mode comes later).
    unsafe { rza1l_hal::gic::register(usb::USB0_IRQ, || dcd_int_handler(0)) };

    // init_device_mode ungates the USB module clock and yields the driver.
    let (_port, driver) = unsafe { init_device_mode(0) };

    let mut config = Config::new(USB_VID, USB_PID);
    config.manufacturer = Some("Synthstrom Audible");
    config.product = Some("Deluge");
    config.self_powered = false;
    config.max_power = 250; // 500 mA

    let mut builder = Builder::new(
        driver,
        config,
        unsafe { &mut *addr_of_mut!(CONFIG_DESC) },
        unsafe { &mut *addr_of_mut!(BOS_DESC) },
        unsafe { &mut *addr_of_mut!(MSOS_DESC) },
        unsafe { &mut *addr_of_mut!(CONTROL_BUF) },
    );

    let (handler, eps) = midi::build(&mut builder);
    let handler_ref = unsafe { (*addr_of_mut!(MIDI_HANDLER)).write(handler) };
    builder.handler(handler_ref);

    let device = builder.build();
    UsbMidi {
        device,
        ep_out: eps.ep_out_midi1,
        ep_in: eps.ep_in_midi1,
    }
}

/// Run the embassy-usb device state machine (enumeration, control, alt-setting).
#[embassy_executor::task]
pub async fn device_task(mut device: UsbDevice<'static, Rusb1Driver>) {
    device.run().await;
}

/// Pump the MIDI 1.0 OUT endpoint (host → device) into the class RX queue.
#[embassy_executor::task]
pub async fn midi_rx_task(ep_out: Rusb1EndpointOut) {
    midi::run_rx_midi1::<Rusb1Driver>(ep_out).await;
}

/// Pump the class TX byte queue out the MIDI 1.0 IN endpoint (device → host).
#[embassy_executor::task]
pub async fn midi_tx_task(ep_in: Rusb1EndpointIn) {
    midi::run_tx_midi1::<Rusb1Driver>(ep_in).await;
}
