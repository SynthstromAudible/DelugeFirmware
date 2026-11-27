//! The timed output service on the host. Stamps count the render cursor, and the play position is
//! the latest block's start plus the Embassy clock's time since it began: the render driver fires
//! the service after each block, and it sends what that clock has reached. A submit only queues, so specs that
//! take output without rendering flush first (`deluge_host_out_flush`). DIN bytes go where the
//! host's DIN writes always went (the simulator, or kept for specs); CV and gate writes are kept as
//! text for specs (`deluge_host_out_take`).

use std::sync::Mutex;

use super::core::{Clock, Config, Outputs, Service};

const CONFIG: Config = Config {
    max_ahead_frames: crate::audio::capture_stamp::INPUT_LATENCY_FRAMES,
    cv_lead_micros: 20,
    tolerance_micros: 22,
};

/// [`CONFIG`] with no stamp in the future: everything queued is due.
const FLUSH: Config = Config {
    max_ahead_frames: 0,
    ..CONFIG
};

static SERVICE: Mutex<Service> = Mutex::new(Service::new());
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
        frame: crate::audio::host::play_frame(),
        micros: 0,
    }
}

/// Run `f` on the service. What it queues goes out at the next [`fire`].
pub(crate) fn service<R>(f: impl FnOnce(&mut Service) -> R) -> R {
    f(&mut SERVICE.lock().unwrap_or_else(|e| e.into_inner()))
}

/// The host's stand-in for the output timer's interrupt, which the render driver calls after each
/// block: send what the codec has reached.
pub(crate) fn fire() {
    let _ = SERVICE
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .fire(&CONFIG, clock(), &mut Host);
}

/// Send everything queued, due or not: for specs, which take output without rendering.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_out_flush() {
    let _ = SERVICE
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .fire(&FLUSH, clock(), &mut Host);
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
