//! The rig harness: lets a host drive the real Deluge with level-3 UI scripts and see what its panel
//! shows, over the debug probe's RTT channel. Built only with the `rig-harness` feature; the pure
//! parts compile everywhere so the host tests can drive them.
//!
//! The app's panel writes are recorded at the libdeluge API, where `ui_harness` records them on
//! host builds, so both harnesses see the same thing.

pub mod input;
pub mod shadow;
pub mod wire;

#[cfg(all(feature = "rig-harness", target_os = "none"))]
mod device {
    use core::cell::RefCell;
    use core::fmt::Write;
    use core::sync::atomic::{AtomicU32, Ordering};

    use critical_section::Mutex;
    use embassy_time::{Duration, Instant, Ticker};

    use super::input::{self, Command};
    use super::shadow::{COLUMN_BYTES, Change, Shadow};
    use super::wire;
    use crate::peripherals::control;
    use crate::sys::{
        DelugeInputEvent, DelugeInputEventKind_DELUGE_EVENT_BUTTON as KIND_BUTTON,
        DelugeInputEventKind_DELUGE_EVENT_NO_PRESSES as KIND_NO_PRESSES,
        DelugeInputEventKind_DELUGE_EVENT_PAD as KIND_PAD,
    };

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

    /// The shortest gap between two OLED lines: the rate tools/emu_parity.py samples the panel at.
    /// Frames at the app's full rate would outrun what the probe reads off the RTT buffer.
    const OLED_INTERVAL_US: u64 = 25_000;

    /// Streams the panel: a heartbeat every 10 ms, then each change since the last one, the OLED
    /// at most every OLED_INTERVAL_US (its latest frame).
    #[embassy_executor::task]
    pub async fn stream_task() {
        let mut ticker = Ticker::every(Duration::from_millis(10));
        let mut last_oled_us = 0u64;
        loop {
            ticker.next().await;
            let us = now_us();
            let oled_due = us.saturating_sub(last_oled_us) >= OLED_INTERVAL_US;
            if oled_due {
                last_oled_us = us;
            }
            line(|l| wire::heartbeat(next_seq(), us, l));
            with(|s| {
                let dropped = s.take_dropped();
                if dropped > 0 {
                    line(|l| wire::dropped(next_seq(), us, dropped, l));
                }
                let changes: heapless::Vec<Change, { Shadow::MAX_CHANGES }> =
                    s.take_changes(oled_due).collect();
                for change in &changes {
                    line(|l| wire::encode(change, s, next_seq(), us, l));
                }
            });
        }
    }

    /// The host's command channel, handed over by the runtime once RTT is set up.
    static DOWN: Mutex<RefCell<Option<rtt_target::DownChannel>>> = Mutex::new(RefCell::new(None));

    pub(crate) fn set_input_channel(channel: rtt_target::DownChannel) {
        critical_section::with(|cs| *DOWN.borrow_ref_mut(cs) = Some(channel));
    }

    fn resend() {
        let us = now_us();
        with(|s| {
            let _ = wire::full_state(s, us, next_seq, emit);
        });
        line(|l| wire::ready(next_seq(), now_us(), l));
    }

    /// Applies one command; false when the app's input queue had no room for it.
    fn apply(cmd: Command, pacer: &mut input::EdgePacer) -> bool {
        let event = |kind, x, y, value| DelugeInputEvent { kind, x, y, value };
        match cmd {
            Command::Button { x, y, down } => control::try_queue_event(event(
                KIND_BUTTON,
                x,
                y,
                if down {
                    input::DEFAULT_VELOCITY as i16
                } else {
                    0
                },
            )),
            Command::Pad { x, y, down, vel } => {
                control::try_queue_event(event(KIND_PAD, x, y, if down { vel as i16 } else { 0 }))
            }
            Command::Encoder { idx, edges } => {
                pacer.add(idx, edges);
                true
            }
            Command::NoPresses => control::try_queue_event(event(KIND_NO_PRESSES, 0, 0, 0)),
            Command::Resend => {
                resend();
                true
            }
        }
    }

    fn refuse(text: &str) {
        line(|l| wire::error(next_seq(), now_us(), text, l));
    }

    /// Reads the host's command lines and applies each; a line that does not parse or does not
    /// fit the input queue comes back as `@E`. Starts with a full panel and `@R`.
    #[embassy_executor::task]
    pub async fn input_task() {
        resend();
        // RTT has no interrupt for host writes, so the channel is polled.
        let mut ticker = Ticker::every(Duration::from_millis(2));
        let mut text: heapless::String<{ input::MAX_LINE }> = heapless::String::new();
        let mut too_long = false;
        let mut pacer = input::EdgePacer::default();
        loop {
            ticker.next().await;
            for (idx, edge) in pacer.tick().into_iter().enumerate() {
                if edge != 0 {
                    control::add_encoder_edges_for_harness(idx as u8, edge);
                }
            }
            let mut buf = [0u8; 64];
            let n = critical_section::with(|cs| {
                DOWN.borrow_ref_mut(cs)
                    .as_mut()
                    .map_or(0, |d| d.read(&mut buf))
            });
            for &b in &buf[..n] {
                if b == b'\n' {
                    if too_long {
                        refuse("(line too long)");
                    } else if !input::parse(&text).is_ok_and(|cmd| apply(cmd, &mut pacer)) {
                        refuse(&text);
                    }
                    text.clear();
                    too_long = false;
                } else if !b.is_ascii() || text.push(b as char).is_err() {
                    too_long = true;
                }
            }
        }
    }
}

#[cfg(all(feature = "rig-harness", target_os = "none"))]
pub(crate) use device::*;
