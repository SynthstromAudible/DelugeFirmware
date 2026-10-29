//! The rig harness: lets a host drive the real Deluge with level-3 UI scripts and see what its panel
//! shows, over the debug probe's RTT channel. Built only with the `rig-harness` feature; the pure
//! parts compile everywhere so the host tests can drive them.
//!
//! The app's panel writes are recorded at the libdeluge API, where `ui_harness` records them on
//! host builds, so both harnesses see the same thing.

pub mod shadow;
pub mod wire;

#[cfg(all(feature = "rig-harness", target_os = "none"))]
mod device {
    use core::cell::RefCell;
    use core::fmt::Write;
    use core::sync::atomic::{AtomicU32, Ordering};

    use critical_section::Mutex;
    use embassy_time::{Duration, Instant, Ticker};

    use super::shadow::{COLUMN_BYTES, Change, Shadow};
    use super::wire;

    // In SDRAM: the SRAM heap has little headroom over its floor. `.sdram_bss` is zeroed at boot,
    // which is `Shadow::new()`.
    #[unsafe(link_section = ".sdram_bss")]
    static SHADOW: Mutex<RefCell<Shadow>> = Mutex::new(RefCell::new(Shadow::new()));

    pub(crate) fn with<R>(f: impl FnOnce(&mut Shadow) -> R) -> R {
        critical_section::with(|cs| f(&mut SHADOW.borrow_ref_mut(cs)))
    }

    pub(crate) fn record_oled(pixels: &[u8], width: u16, height: u16) {
        with(|s| s.record_oled(pixels, width, height));
    }

    pub(crate) fn record_columns(idx: u8, rgb: &[[u8; 3]; 16]) {
        let mut flat = [0u8; COLUMN_BYTES];
        flat.copy_from_slice(rgb.as_flattened());
        with(|s| s.record_columns(idx, &flat));
    }

    pub(crate) fn record_led(id: u8, on: bool) {
        with(|s| s.record_led(id, on));
    }

    pub(crate) fn record_ring(knob: u8, levels: [u8; 4]) {
        with(|s| s.record_ring(knob, levels));
    }

    pub(crate) fn record_flash(idx: u8, colour: u8) {
        with(|s| s.record_flash(idx, colour));
    }

    pub(crate) fn record_pic(dimmer: Option<u8>, refresh: Option<u8>) {
        with(|s| s.record_pic(dimmer, refresh));
    }

    pub(crate) fn record_scroll() {
        with(|s| s.record_scroll());
    }

    /// Numbers every `@` line, so the host sees a gap when RTT drops one.
    static SEQ: AtomicU32 = AtomicU32::new(0);

    pub(crate) fn next_seq() -> u32 {
        SEQ.fetch_add(1, Ordering::Relaxed)
    }

    pub(crate) fn now_us() -> u64 {
        Instant::now().as_micros()
    }

    /// Writes one whole line to RTT channel 0, beside the log lines. In the channel's NoBlockSkip
    /// mode a line that does not fit is skipped whole, which the host sees as a gap in `seq`.
    pub(crate) fn emit(line: &str) {
        rtt_target::with_terminal_channel(|t| {
            let _ = t.write(0).write_str(line);
        });
    }

    fn line(f: impl FnOnce(&mut heapless::String<{ wire::MAX_LINE }>) -> core::fmt::Result) {
        let mut l = heapless::String::new();
        if f(&mut l).is_ok() {
            emit(&l);
        }
    }

    /// Streams the panel: a heartbeat every 10 ms, then each change since the last one.
    #[embassy_executor::task]
    pub async fn stream_task() {
        let mut ticker = Ticker::every(Duration::from_millis(10));
        loop {
            ticker.next().await;
            let us = now_us();
            line(|l| wire::heartbeat(next_seq(), us, l));
            with(|s| {
                let dropped = s.take_dropped();
                if dropped > 0 {
                    line(|l| wire::dropped(next_seq(), us, dropped, l));
                }
                let changes: heapless::Vec<Change, { Shadow::MAX_CHANGES }> =
                    s.take_changes().collect();
                for change in &changes {
                    line(|l| wire::encode(change, s, next_seq(), us, l));
                }
            });
        }
    }
}

#[cfg(all(feature = "rig-harness", target_os = "none"))]
pub(crate) use device::*;
