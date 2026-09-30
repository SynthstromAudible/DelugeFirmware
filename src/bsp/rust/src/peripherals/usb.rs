//! USB device bring-up: the Deluge enumerates as a class-compliant USB-MIDI 1.0 device.
//!
//! USB host mode (MIDI devices plugged into the Deluge) is not implemented, so the libdeluge USB
//! role is fixed to peripheral. The embassy-usb device and the MIDI class run as background tasks
//! on the app's executor; the app reaches the byte stream through [`midi`](super::midi), port 1.

use core::mem::MaybeUninit;
use core::ptr::addr_of_mut;

use deluge_bsp::usb::classes::midi::{self, MidiClassHandler};
use embassy_usb::{Builder, Config, UsbDevice};
use rza1l_hal::usb::{
    self, Rusb1Driver, Rusb1EndpointIn, Rusb1EndpointOut, dcd_int_handler, init_device_mode,
};

// Descriptor and control buffers for the embassy-usb builder, sized generously for a MIDI-only
// device.
static mut CONFIG_DESC: [u8; 256] = [0; 256];
static mut BOS_DESC: [u8; 64] = [0; 64];
static mut MSOS_DESC: [u8; 0] = [];
static mut CONTROL_BUF: [u8; 64] = [0; 64];
/// The MIDI class handler's `'static` home (`Builder::handler` takes a `'static` reference).
static mut MIDI_HANDLER: MaybeUninit<MidiClassHandler> = MaybeUninit::uninit();

// A development USB identity, not a product ID. TODO: replace with an owned VID/PID before
// distribution (see `deluge_bsp::usb::ids`).
const USB_VID: u16 = 0x16D0;
const USB_PID: u16 = 0x0EDA;

/// The built USB device and its MIDI 1.0 bulk endpoints, to be spawned as background tasks.
pub struct UsbMidi {
    pub device: UsbDevice<'static, Rusb1Driver>,
    pub ep_out: Rusb1EndpointOut,
    pub ep_in: Rusb1EndpointIn,
}

/// Brings up USB0 in device mode and builds the USB-MIDI 1.0 device.
///
/// Registers the USB0 GIC handler; the driver enables the GIC line later, from `device.run()`.
///
/// # Safety
///
/// Touches USB and GIC registers and the static USB buffers: call exactly once, during
/// single-threaded startup, before interrupts are unmasked.
pub unsafe fn build() -> UsbMidi {
    // Device-mode dispatch only: host mode is not implemented.
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

/// Background task: runs the embassy-usb device state machine (enumeration, control requests).
#[embassy_executor::task]
pub async fn device_task(mut device: UsbDevice<'static, Rusb1Driver>) {
    device.run().await;
}

/// Background task: pumps the MIDI 1.0 OUT endpoint (computer → Deluge) into the class RX queue.
#[embassy_executor::task]
pub async fn midi_rx_task(ep_out: Rusb1EndpointOut) {
    midi::run_rx_midi1::<Rusb1Driver>(ep_out).await;
}

/// Background task: pumps the class TX byte queue out the MIDI 1.0 IN endpoint (Deluge →
/// computer).
#[embassy_executor::task]
pub async fn midi_tx_task(ep_in: Rusb1EndpointIn) {
    midi::run_tx_midi1::<Rusb1Driver>(ep_in).await;
}
