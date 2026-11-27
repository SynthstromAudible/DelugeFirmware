//! The timed output service on the host: no play position, so every stamp the app makes is
//! [`STAMP_NOW`](super::core::STAMP_NOW) and goes out in the call that submits it. DIN bytes go
//! where the host's DIN writes always went (the simulator, or kept for specs); CV and gate writes
//! are kept as text for specs (`deluge_host_out_take`).

use std::sync::Mutex;

use super::core::{Clock, Config, Outputs, Service};

const CONFIG: Config = Config {
    max_ahead_frames: crate::audio::capture_stamp::INPUT_LATENCY_FRAMES,
    cv_lead_micros: 20,
    tolerance_micros: 22,
};

static SERVICE: Mutex<Service> = Mutex::new(Service::new());
/// The armed deadline on the Embassy clock, in µs, or `u64::MAX`.
static DEADLINE: core::sync::atomic::AtomicU64 = core::sync::atomic::AtomicU64::new(u64::MAX);
/// CV and gate writes, one per line, oldest first.
static LOG: Mutex<String> = Mutex::new(String::new());

struct Host;

impl Outputs for Host {
    fn din_write(&mut self, bytes: &[u8]) -> usize {
        crate::host_link_stubs::host_din_write(bytes);
        bytes.len()
    }
    fn usb_send(&mut self, _packet: [u8; 4]) -> bool {
        true
    }
    fn gate_set(&mut self, channel: u8, level: bool) {
        use core::fmt::Write;
        let mut log = LOG.lock().unwrap_or_else(|e| e.into_inner());
        let _ = writeln!(log, "gate{channel}={}", level as u8);
    }
    fn cv_write(&mut self, channel: u8, value: u16) -> bool {
        use core::fmt::Write;
        let mut log = LOG.lock().unwrap_or_else(|e| e.into_inner());
        let _ = writeln!(log, "cv{channel}={value}");
        true
    }
}

fn clock() -> Clock {
    Clock {
        frame: 0,
        micros: embassy_time::Instant::now().as_micros(),
    }
}

/// As the device's: run `f`, send what is due, keep the deadline for [`fire`].
pub(crate) fn service<R>(f: impl FnOnce(&mut Service) -> R) -> R {
    let mut s = SERVICE.lock().unwrap_or_else(|e| e.into_inner());
    let result = f(&mut s);
    let at = s.fire(&CONFIG, clock(), &mut Host);
    DEADLINE.store(
        at.unwrap_or(u64::MAX),
        core::sync::atomic::Ordering::Relaxed,
    );
    result
}

/// The host's stand-in for the output timer's interrupt, which the render driver calls after each
/// block: send what has come due.
pub(crate) fn fire() {
    let at = DEADLINE.load(core::sync::atomic::Ordering::Relaxed);
    if at != u64::MAX && embassy_time::Instant::now().as_micros() >= at {
        service(|_| ());
    }
}

/// Moves whole lines of the CV and gate log into `dst` (at most `cap` bytes); returns the bytes
/// moved.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_out_take(dst: *mut u8, cap: u32) -> u32 {
    let mut log = LOG.lock().unwrap_or_else(|e| e.into_inner());
    let mut n = 0;
    for line in log.split_inclusive('\n') {
        if n + line.len() > cap as usize {
            break;
        }
        n += line.len();
    }
    // SAFETY: the caller passes `cap` writable bytes; `n` <= cap.
    unsafe { core::ptr::copy_nonoverlapping(log.as_ptr(), dst, n) };
    log.drain(..n);
    n as u32
}
