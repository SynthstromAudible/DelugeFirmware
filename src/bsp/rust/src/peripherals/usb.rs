//! The USB device (`libdeluge/usb_audio.h`'s `deluge_usb_device_start`): the Deluge enumerates as
//! a class-compliant USB-MIDI 1.0 device and, when started with audio, a USB Audio Class 2.0 one.
//!
//! USB host mode (MIDI devices plugged into the Deluge) is not implemented, so the libdeluge USB
//! role is fixed to peripheral. Boot brings the port up ([`init`]) but leaves it off the bus;
//! [`usb_task`] waits for the app's start, builds the configuration it asked for, and runs the
//! device and the MIDI pumps. The app reaches the USB-MIDI event-packet stream through
//! [`midi`](super::midi), port 1, and the audio streams through [`usb_audio`](super::usb_audio).

use core::mem::MaybeUninit;
use core::ptr::addr_of_mut;
use core::sync::atomic::{AtomicU8, Ordering};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::signal::Signal;
use embassy_usb::{Builder, Config};
use rza1_hal::usb::{Rusb1Driver, init_device_mode};
use usb_classes::midi::{self, MidiClassHandler};

use super::usb_audio::{self, AudioHandler};

// Descriptor and control buffers for the embassy-usb builder. The configuration descriptor holds
// USB-MIDI and the UAC2 function's AudioControl and two streaming interfaces, with room to spare.
static mut CONFIG_DESC: [u8; 768] = [0; 768];
static mut BOS_DESC: [u8; 64] = [0; 64];
static mut MSOS_DESC: [u8; 0] = [];
static mut CONTROL_BUF: [u8; 64] = [0; 64];
/// The class handlers' `'static` homes (`Builder::handler` takes a `'static` reference).
static mut MIDI_HANDLER: MaybeUninit<MidiClassHandler> = MaybeUninit::uninit();
static mut AUDIO_HANDLER: MaybeUninit<AudioHandler> = MaybeUninit::uninit();

// A development USB identity, not a product ID. TODO: replace with an owned VID/PID before
// distribution (see `deluge_bsp::usb::ids`).
const USB_VID: u16 = 0x16D0;
const USB_PID: u16 = 0x0EDA;

rza1_hal::bind_interrupts!(struct UsbIrqs {
    USB0 => rza1_hal::usb::DeviceInterruptHandler<rza1_hal::peripherals::USB0>;
});

/// [`STARTED`]'s states: not started, started with USB-MIDI only, started with audio as well.
const NOT_STARTED: u8 = 0;
const STARTED_MIDI: u8 = 1;
const STARTED_WITH_AUDIO: u8 = 2;

/// Which configuration the app started the device with; set once.
static STARTED: AtomicU8 = AtomicU8::new(NOT_STARTED);
/// Wakes [`usb_task`] once [`STARTED`] is set.
static START: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Brings up USB0 in device mode, off the bus, and returns its driver for [`usb_task`].
///
/// Binds the USB0 GIC handler; the driver enables the GIC line, and pulls up D+, when the device
/// runs.
///
/// # Safety
///
/// Touches USB and GIC registers: call exactly once, during single-threaded startup, before
/// interrupts are unmasked.
pub unsafe fn init(
    usb0: rza1_hal::Peri<'static, rza1_hal::peripherals::USB0>,
) -> Rusb1Driver<'static> {
    // init_device_mode binds the device-mode handler (host mode is not implemented), ungates the
    // USB module clock and yields the driver.
    let (_port, driver) = init_device_mode(usb0, UsbIrqs);
    driver
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_usb_device_start(with_audio: bool) {
    let config = if with_audio {
        STARTED_WITH_AUDIO
    } else {
        STARTED_MIDI
    };
    if STARTED
        .compare_exchange(NOT_STARTED, config, Ordering::AcqRel, Ordering::Acquire)
        .is_ok()
    {
        START.signal(());
    }
}

abi_pin!(deluge_usb_device_start);

/// Background task: waits for [`deluge_usb_device_start`], builds the device it asked for, then
/// runs the embassy-usb device state machine (enumeration, control requests) and the two MIDI 1.0
/// endpoint pumps. The audio streams need no task: their packets move in the USB0 interrupt.
#[embassy_executor::task]
pub async fn usb_task(driver: Rusb1Driver<'static>) {
    START.wait().await;
    let with_audio = STARTED.load(Ordering::Acquire) == STARTED_WITH_AUDIO;

    let mut config = Config::new(USB_VID, USB_PID);
    config.manufacturer = Some("Synthstrom Audible");
    config.product = Some("Deluge");
    config.self_powered = false;
    config.max_power = 250; // 500 mA

    // SAFETY: this task runs once, so the buffers and handler homes have no other user.
    let mut builder = unsafe {
        Builder::new(
            driver,
            config,
            &mut *addr_of_mut!(CONFIG_DESC),
            &mut *addr_of_mut!(BOS_DESC),
            &mut *addr_of_mut!(MSOS_DESC),
            &mut *addr_of_mut!(CONTROL_BUF),
        )
    };

    // The audio function goes first: its interface numbers are fixed from 0.
    if with_audio {
        let handler = usb_audio::build(&mut builder);
        // SAFETY: written once, here; the builder holds the only reference from now on.
        let handler_ref = unsafe { (*addr_of_mut!(AUDIO_HANDLER)).write(handler) };
        builder.handler(handler_ref);
    }

    // The app speaks whole USB-MIDI event packets (SysEx included), so the class passes them
    // through instead of decoding them; selected before the endpoint pumps run.
    midi::use_packets();
    let (handler, eps) = midi::build(&mut builder);
    // SAFETY: as above.
    let handler_ref = unsafe { (*addr_of_mut!(MIDI_HANDLER)).write(handler) };
    builder.handler(handler_ref);

    let mut device = builder.build();
    log::info!(
        "usb: device started ({})",
        if with_audio { "MIDI + audio" } else { "MIDI" }
    );
    embassy_futures::join::join3(
        device.run(),
        // MIDI 1.0 OUT endpoint (computer → Deluge) into the class RX packet queue.
        midi::run_rx_midi1::<Rusb1Driver<'static>>(eps.ep_out_midi1),
        // The class TX packet queue out the MIDI 1.0 IN endpoint (Deluge → computer).
        midi::run_tx_midi1::<Rusb1Driver<'static>>(eps.ep_in_midi1),
    )
    .await;
}
