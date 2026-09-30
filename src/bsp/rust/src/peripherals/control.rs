//! `libdeluge/control_surface.h` and `libdeluge/encoder_io.h` — pads and buttons (via the PIC),
//! LEDs, and encoders.
//!
//! [`pic_pump`] decodes the PIC byte stream into `EVENTS`; [`pad_render`] is the sole PIC
//! transmitter. The synchronous boundary functions only queue and drain. The one-time UART and
//! encoder GPIO/GIC setup runs in the device runtime's pre-executor window, with IRQs masked.
#![allow(non_snake_case)]

use core::sync::atomic::{AtomicBool, AtomicI8, AtomicU16, Ordering};

use deluge_bsp::{encoder, pic};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;

use crate::sys::{
    DelugeBootInfo, DelugeColour, DelugeInputEvent,
    DelugeInputEventKind_DELUGE_EVENT_BUTTON as KIND_BUTTON,
    DelugeInputEventKind_DELUGE_EVENT_NO_PRESSES as KIND_NO_PRESSES,
    DelugeInputEventKind_DELUGE_EVENT_PAD as KIND_PAD,
};

/// Latched PIC boot info for [`deluge_control_read_boot_info`]: 0 until received, then the raw
/// firmware-version byte OR-ed with [`BOOT_FW_RECEIVED`].
///
/// [`pic_pump`] latches it from the `FirmwareVersion` reply to `pic::init`'s request; boot awaits
/// `storage::sd::boot_init()` before `deluge_app_init` reads it, which gives the pump time to run.
static BOOT_FW: AtomicU16 = AtomicU16::new(0);
/// "Received" sentinel bit OR-ed into [`BOOT_FW`] (the raw byte is 8 bits).
const BOOT_FW_RECEIVED: u16 = 0x100;

// ABI guard: the header pins `enum DelugeInputEventKind : uint8_t`, so `DelugeInputEvent` is
// {kind@0, x@1, y@2, value@4} = 6 bytes on every target. A wider enum would make the app read
// x/y/value from the wrong offsets, silently; fail the build instead.
const _: () = {
    assert!(
        core::mem::size_of::<DelugeInputEvent>() == 6,
        "DelugeInputEvent ABI drift: must be 6 bytes (uint8_t-sized kind, explicit width)"
    );
    assert!(
        core::mem::size_of::<crate::sys::DelugeInputEventKind>() == 1,
        "DelugeInputEventKind must be 1 byte (explicit `: uint8_t` underlying type)"
    );
};

/// `CONTROL_DEFAULT_VELOCITY`: 255 means "use the instrument default"; 0 is a release.
const DEFAULT_VELOCITY: i16 = 255;

/// Decoded PIC input events, produced by [`pic_pump`] and drained by
/// [`deluge_control_poll_event`].
///
/// Bounded: overflow drops rather than blocking the RX pump (only reachable during the
/// non-yielding boot phase).
static EVENTS: Channel<CriticalSectionRawMutex, DelugeInputEvent, 64> = Channel::new();

/// Background task: runs the PIC baud-rate handshake, then decodes its byte stream into
/// `EVENTS`.
///
/// OLED chip-select replies go to the `pic` notifier for the display path. Encoder motion never
/// arrives via the PIC; it comes from GPIO IRQs (see [`deluge_encoder_take_edges`]).
#[embassy_executor::task]
pub async fn pic_pump() {
    pic::init().await;
    log::info!("deluge-rust: PIC initialised, pumping input events");
    let mut parser = pic::Parser::new();
    loop {
        let byte = pic::read_byte().await;
        let Some(event) = parser.push(byte) else {
            continue;
        };
        let mapped = match event {
            pic::Event::PadPress { id } => Some(pad_event(id, DEFAULT_VELOCITY)),
            pic::Event::PadRelease { id } => Some(pad_event(id, 0)),
            pic::Event::ButtonPress { id } => Some(button_event(id, DEFAULT_VELOCITY)),
            pic::Event::ButtonRelease { id } => Some(button_event(id, 0)),
            pic::Event::NoPresses => Some(DelugeInputEvent {
                kind: KIND_NO_PRESSES,
                x: 0,
                y: 0,
                value: 0,
            }),
            pic::Event::OledSelected => {
                pic::notify_oled_selected();
                None
            }
            pic::Event::OledDeselected => {
                pic::notify_oled_deselected();
                None
            }
            pic::Event::FirmwareVersion(v) => {
                // Latch the raw byte: high bit = OLED present, low 7 bits = version.
                BOOT_FW.store((v as u16) | BOOT_FW_RECEIVED, Ordering::Relaxed);
                log::info!(
                    "deluge-rust: PIC firmware v{} (oled={})",
                    v & 0x7f,
                    (v & 0x80) != 0
                );
                None
            }
            // pic::Event is #[non_exhaustive]; ignore any future variants.
            _ => None,
        };
        if let Some(ev) = mapped {
            let _ = EVENTS.try_send(ev);
        }
    }
}

/// Maps a PIC pad id to a grid event at (x, y), as the app's `Pad(uint8_t)` does.
fn pad_event(id: u8, value: i16) -> DelugeInputEvent {
    let (x, y) = pic::pad_coords(id);
    DelugeInputEvent {
        kind: KIND_PAD,
        x,
        y,
        value,
    }
}

/// Maps a PIC button id (raw byte − 144) to a matrix event at (id % 9, id / 9).
fn button_event(id: u8, value: i16) -> DelugeInputEvent {
    DelugeInputEvent {
        kind: KIND_BUTTON,
        x: id % 9,
        y: id / 9,
        value,
    }
}

/// Writes the PIC boot info (firmware version, OLED present, factory-reset request) to `*out`.
///
/// `factory_reset_requested` is always false: the PIC parser does not decode the boot
/// reset-burst. Every field must be written — an uninitialised struct makes the app read stack
/// garbage and factory-reset the settings.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_read_boot_info(out: *mut DelugeBootInfo) {
    let fw = BOOT_FW.load(Ordering::Relaxed);
    let (version, oled_present) = if fw & BOOT_FW_RECEIVED != 0 {
        ((fw & 0x7f) as u8, (fw & 0x80) != 0)
    } else {
        // The version byte did not arrive in time; this board has the OLED.
        (0, true)
    };
    // SAFETY: the app passes a valid DelugeBootInfo out-pointer (sync boot call).
    unsafe {
        (*out).pic_firmware_version = version;
        (*out).oled_present = oled_present;
        (*out).factory_reset_requested = false;
    }
}

/// Drains one decoded input event into `*out`; returns false when none is buffered.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_poll_event(out: *mut DelugeInputEvent) -> bool {
    match EVENTS.try_receive() {
        Ok(ev) => {
            // SAFETY: the app passes a valid DelugeInputEvent out-pointer.
            unsafe { *out = ev };
            true
        }
        Err(_) => false,
    }
}

/// Maps the app's encoder ordinal (`hid::encoders::EncoderName`) to the index in
/// `deluge_bsp::encoder::ENCODER_DELTAS`, which is in pin order.
///
/// Derived by matching pins against `encoder::irq_init`'s setup table:
/// SCROLL_Y(8/10)→4, SCROLL_X(11/12)→0, TEMPO(6/7)→1, SELECT(3/2)→5, MOD_1(5/4)→3,
/// MOD_0(0/15)→2. The invert flags line up.
const ENC_REMAP: [usize; encoder::NUM_ENCODERS] = [4, 0, 1, 5, 3, 2];

/// Atomically reads and clears the signed A-pin edge count the quadrature ISRs accumulated for
/// `encoder` (an app ordinal). Raw edges: the app applies its own detent policy.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_encoder_take_edges(encoder: u8) -> i8 {
    let i = encoder as usize;
    if i >= encoder::NUM_ENCODERS {
        return 0;
    }
    encoder::ENCODER_DELTAS[ENC_REMAP[i]].swap(0, Ordering::Relaxed)
}

// Control-surface output (LEDs, pads)
//
// Every PIC transmit goes through one task, `pad_render`, draining a command queue. The PIC TX
// path holds an async mutex across each transfer, so a `block_on` of a PIC call from a tick could
// deadlock against a task holding it. The boundary functions are non-blocking producers that drop
// on overflow; a dropped frame self-heals on the next redraw.

/// One queued control-surface output operation.
#[derive(Clone, Copy)]
enum PicOut {
    /// 16 RGB colours for column-pair `idx` (the app pre-packs the wire layout).
    Columns { idx: u8, colours: [[u8; 3]; 16] },
    /// Indicator LED on/off (board-defined index).
    Led { id: u8, on: bool },
    /// Gold-knob/mod indicator ring brightnesses.
    Indicator { knob: u8, levels: [u8; 4] },
    /// Surface refresh interval (board units).
    RefreshTime(u8),
    /// "Done sending rows" → trigger the PIC's display refresh.
    Refresh,
    /// Begin a horizontal scroll animation; `flags` is the app's direction/full-width encoding.
    ScrollSetup(u8),
    /// The incoming colour for one row of an in-progress horizontal scroll.
    ScrollRow { row: u8, colour: [u8; 3] },
    /// A vertical scroll, carrying the whole incoming row (18 columns).
    ScrollVertical {
        up: bool,
        colours: [[u8; 3]; 18],
        count: u8,
    },
}

/// The most bytes one queued command becomes on the PIC wire (a column pair: 1 + 16·3).
///
/// [`deluge_control_pad_output_space`] reports in bytes because the app's redraw gates compare
/// against its `kNumBytesIn*` constants.
const BYTES_PER_CMD: u32 = 49;

/// The output command queue, drained by [`pad_render`].
///
/// Sized so a full grid and sidebar redraw (≈10–18 commands) leaves plenty free: the app gates
/// redraws on [`deluge_control_pad_output_space`], derived from the free slot count.
static OUT: Channel<CriticalSectionRawMutex, PicOut, 64> = Channel::new();
/// Set by any output op; [`deluge_control_flush`] turns it into a single refresh, so an idle tick
/// sends nothing.
static OUTPUT_DIRTY: AtomicBool = AtomicBool::new(false);

/// Background task: the sole PIC transmitter. Drains `OUT`, yielding between sends so the RX
/// pump and the app keep running.
#[embassy_executor::task]
pub async fn pad_render() {
    // A command sent mid-handshake would go out at the wrong baud rate; boot-time commands wait
    // in OUT.
    pic::wait_ready().await;
    loop {
        match OUT.receive().await {
            PicOut::Columns { idx, colours } => pic::set_column_pair_rgb(idx, &colours).await,
            PicOut::Led { id, on } => {
                if on {
                    pic::led_on(id).await
                } else {
                    pic::led_off(id).await
                }
            }
            PicOut::Indicator { knob, levels } => pic::set_gold_knob_indicators(knob, levels).await,
            PicOut::RefreshTime(ms) => pic::set_refresh_time(ms).await,
            PicOut::Refresh => pic::done_sending_rows().await,
            PicOut::ScrollSetup(flags) => pic::setup_horizontal_scroll(flags).await,
            PicOut::ScrollRow { row, colour } => pic::send_scroll_row(row, colour).await,
            PicOut::ScrollVertical { up, colours, count } => {
                pic::vertical_scroll(up, &colours[..(count as usize).min(18)]).await
            }
        }
    }
}

#[inline]
fn enqueue(cmd: PicOut) {
    // Drop on overflow rather than block the app; a dropped frame self-heals on the next redraw.
    let _ = OUT.try_send(cmd);
    OUTPUT_DIRTY.store(true, Ordering::Relaxed);
}

/// Sets the colours of a main-grid column pair from `count` entries of `colours` (the app sends
/// 16, in PIC wire order; at most 16 are used).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_pad_columns(idx: u8, colours: *const DelugeColour, count: u8) {
    let mut buf = [[0u8; 3]; 16];
    let n = (count as usize).min(16);
    // SAFETY: the app passes `count` valid DelugeColour entries.
    let src = unsafe { core::slice::from_raw_parts(colours, n) };
    for (dst, c) in buf.iter_mut().zip(src) {
        *dst = [c.r, c.g, c.b];
    }
    enqueue(PicOut::Columns { idx, colours: buf });
}

/// Begins a horizontal scroll animation on the surface.
///
/// The PIC animates from its own off-screen framebuffer: the app calls this once, then feeds one
/// colour per row per tick via [`deluge_control_scroll_row`], ending each tick with
/// [`deluge_control_scroll_done`]. This avoids resending the whole grid every ~7 ms.
///
/// `flags`: bit 0 = positive direction; bit 1 = the area includes the sidebar (18 columns, not
/// 16). Forwarded as-is.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_horizontal(flags: u8) {
    enqueue(PicOut::ScrollSetup(flags));
}

/// Supplies the incoming colour for one row (0–7) of an in-progress horizontal scroll.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_row(row: u8, colour: DelugeColour) {
    enqueue(PicOut::ScrollRow {
        row,
        colour: [colour.r, colour.g, colour.b],
    });
}

/// Ends a scroll tick: tells the PIC this step's row data is complete, which makes it refresh.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_done() {
    enqueue(PicOut::Refresh);
}

/// Performs a vertical scroll, supplying the whole incoming row: `count` entries of `colours`, one
/// per column (the app sends all 18; at most 18 are used).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_vertical(
    up: bool,
    colours: *const DelugeColour,
    count: u8,
) {
    let mut buf = [[0u8; 3]; 18];
    let n = (count as usize).min(18);
    // SAFETY: the app passes `count` valid DelugeColour entries.
    let src = unsafe { core::slice::from_raw_parts(colours, n) };
    for (dst, c) in buf.iter_mut().zip(src) {
        *dst = [c.r, c.g, c.b];
    }
    enqueue(PicOut::ScrollVertical {
        up,
        colours: buf,
        count: n as u8,
    });
}

/// Sets an indicator LED (board-defined index) on or off.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_led(led: u8, on: bool) {
    enqueue(PicOut::Led { id: led, on });
}

/// Sets a gold-knob indicator ring: `which` selects the knob (0 or non-zero); `levels` holds
/// `count` per-LED brightnesses (the ring has 4; at most 4 are used).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_indicator(which: u8, levels: *const u8, count: u8) {
    let mut ring = [0u8; 4];
    let n = (count as usize).min(4);
    // SAFETY: the app passes `count` valid brightness bytes.
    let src = unsafe { core::slice::from_raw_parts(levels, n) };
    ring[..n].copy_from_slice(&src[..n]);
    enqueue(PicOut::Indicator {
        knob: (which != 0) as u8,
        levels: ring,
    });
}

/// Sets the surface refresh interval (board units).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_refresh_time(ms: u8) {
    let _ = OUT.try_send(PicOut::RefreshTime(ms));
}

/// Returns the free space for queued pad output, in bytes: free queue slots × `BYTES_PER_CMD`.
///
/// The app gates redraws on this against byte counts, so it must not be a slot count.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_pad_output_space() -> u32 {
    OUT.free_capacity() as u32 * BYTES_PER_CMD
}

/// Flushes batched surface output: queues one refresh if anything was queued since the last
/// flush.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flush() {
    if OUTPUT_DIRTY.swap(false, Ordering::Relaxed) {
        let _ = OUT.try_send(PicOut::Refresh);
    }
}

/// The scheduler task id of the app's encoder task, from [`deluge_encoder_io_init`]; -1 until
/// then. [`encoder_wake_pump`] unblocks it on movement.
static ENCODER_WAKE_TASK: AtomicI8 = AtomicI8::new(-1);

/// Records `wake_task`, the app's encoder task, for [`encoder_wake_pump`] to wake.
///
/// The GPIO/GIC bring-up already ran in the pre-executor window (it must precede interrupt
/// enable). The app's `interpretEncodersTask` blocks itself after each run and relies on the board
/// to unblock it when an encoder moves; without the wake the encoders go dead.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_encoder_io_init(wake_task: i8) {
    ENCODER_WAKE_TASK.store(wake_task, Ordering::Relaxed);
    log::info!("deluge-rust: deluge_encoder_io_init (encoder wake task id={wake_task})");
}

/// Background task: bridges the encoder edge ISRs to the scheduler.
///
/// Waits on `encoder::ENCODER_WAKER` and unblocks the app's encoder task so it drains the edges.
/// Without it, encoder input never reaches the app.
#[embassy_executor::task]
pub async fn encoder_wake_pump() {
    loop {
        // Register the waker before checking the deltas so an ISR in the gap can't be missed.
        core::future::poll_fn(|cx| {
            encoder::ENCODER_WAKER.register(cx.waker());
            if encoder::ENCODER_DELTAS
                .iter()
                .any(|d| d.load(Ordering::Relaxed) != 0)
            {
                core::task::Poll::Ready(())
            } else {
                core::task::Poll::Pending
            }
        })
        .await;

        let id = ENCODER_WAKE_TASK.load(Ordering::Relaxed);
        if id >= 0 {
            crate::scheduler::unblockTask(id);
        }
        // Bound the unblock rate and let the encoder task drain the deltas before the re-check
        // (else this spins until it runs). 1 ms is imperceptible for knob turns.
        embassy_time::Timer::after(embassy_time::Duration::from_millis(1)).await;
    }
}

abi_pin!(
    deluge_control_flush,
    deluge_control_pad_output_space,
    deluge_control_poll_event,
    deluge_control_read_boot_info,
    deluge_control_scroll_done,
    deluge_control_scroll_horizontal,
    deluge_control_scroll_row,
    deluge_control_scroll_vertical,
    deluge_control_set_indicator,
    deluge_control_set_led,
    deluge_control_set_pad_columns,
    deluge_control_set_refresh_time,
    deluge_encoder_io_init,
    deluge_encoder_take_edges,
);
