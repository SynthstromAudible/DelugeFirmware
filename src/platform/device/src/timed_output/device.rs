//! The timed output service on the device: its queues in SDRAM, filled under a critical section
//! and sent from there or from the MTU2 one-shot's interrupt.

use core::cell::RefCell;

use critical_section::Mutex;
use deluge_bsp::{cv_gate, uart};
use usb_classes::midi as usb_midi;

use super::core::{Clock, Config, Outputs, Service};

/// The service's timing. The stale bound is the output latency, the furthest ahead the render
/// stamps anything.
pub(crate) const CONFIG: Config = Config {
    max_ahead_frames: crate::audio::capture_stamp::INPUT_LATENCY_FRAMES,
    // The SPI word and the MAX5136's settling.
    cv_lead_micros: 20,
    // About a sample.
    tolerance_micros: 22,
};

// In SDRAM (`.sdram_bss`, zeroed at boot: an all-zero Service is empty) to spare internal SRAM.
#[unsafe(link_section = ".sdram_bss")]
static SERVICE: Mutex<RefCell<Service>> = Mutex::new(RefCell::new(Service::new()));

struct Hardware;

impl Outputs for Hardware {
    fn din_write(&mut self, bytes: &[u8]) -> usize {
        uart::try_write_midi(bytes)
    }
    fn usb_send(&mut self, packet: [u8; 4]) -> bool {
        // A packet for a host that has gone is dropped, not retried.
        !usb_midi::connected() || usb_midi::try_send_packet_to_host(packet)
    }
    fn gate_set(&mut self, channel: u8, level: bool) {
        if (channel as usize) < cv_gate::NUM_GATE_CHANNELS {
            cv_gate::gate_set(channel, level);
        }
    }
    fn cv_write(&mut self, channel: u8, value: u16) -> bool {
        cv_gate::try_cv_set_now(channel, value)
    }
}

fn clock() -> Clock {
    Clock {
        frame: crate::audio::device::play_timeline_frame(),
        micros: embassy_time::Instant::now().as_micros(),
    }
}

/// Run `f` on the service with its lock held, then send what is due and arm the output timer for
/// the rest. Every submit and the timer's interrupt come through here.
pub(crate) fn service<R>(f: impl FnOnce(&mut Service) -> R) -> R {
    critical_section::with(|cs| {
        let mut s = SERVICE.borrow_ref_mut(cs);
        let result = f(&mut s);
        match s.fire(&CONFIG, clock(), &mut Hardware) {
            Some(at) => crate::peripherals::signals::output_timer_arm_at(at),
            None => crate::peripherals::signals::output_timer_cancel(),
        }
        result
    })
}
