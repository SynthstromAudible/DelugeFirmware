//! control_surface.h + encoder_io.h — pads/buttons (PIC) and encoders.
//!
//! Because the app now yields between ticks (deluge_app_tick), deluge-bsp's async
//! PIC pump task runs concurrently: it decodes the PIC byte stream into events and
//! buffers them in [`EVENTS`]. The synchronous boundary functions are thin shims —
//! [`deluge_control_poll_event`] drains the buffer; [`deluge_encoder_take_edges`]
//! reads the ISR-accumulated quadrature edge counters. The one-time UART (SCIF1
//! DMA RX/TX) and encoder GPIO/GIC setup happens in `main`'s pre-executor window
//! (`uart::init_pic` / `encoder::irq_init`), with IRQs masked.
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

/// Latched PIC boot info for [`deluge_control_read_boot_info`]. 0 = not yet
/// received; otherwise the raw firmware-version byte OR-ed with [`BOOT_FW_RECEIVED`].
/// The pic_pump captures it from the `FirmwareVersion` event during boot (the byte
/// arrives in response to `pic::init`'s REQUEST_FIRMWARE_VERSION, and the app yields
/// to the pump — via `sd::boot_init().await` — before `deluge_app_init` reads it).
static BOOT_FW: AtomicU16 = AtomicU16::new(0);
/// "Received" sentinel bit OR-ed into [`BOOT_FW`] (raw byte is only 8 bits).
const BOOT_FW_RECEIVED: u16 = 0x100;

// ABI guard: DelugeInputEventKind's underlying type is pinned explicitly in
// the header (`enum DelugeInputEventKind : uint8_t`, control_surface.h), so
// it is 1 byte on every target — device (arm), host_app, and sim alike —
// regardless of `-fshort-enums` (which stays removed from the build; see
// build.rs). DelugeInputEvent is therefore {kind@0 (1B), x@1, y@2, value@4}
// = 6 bytes (the i16 `value` needs 2-byte alignment, leaving one pad byte
// after `y`). bindgen must match; if this ever regresses to a wider enum the
// app reads x/y/value from the wrong offsets (silently, as garbage) — this
// class of bug is real and hard to find (see the enum-width note on
// DelugeRegionState in sample_source.h). Fail the build instead.
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

/// CONTROL_DEFAULT_VELOCITY: 255 = "use the instrument default"; 0 = release.
const DEFAULT_VELOCITY: i16 = 255;

/// Decoded PIC input events, produced by [`pic_pump`], drained by
/// [`deluge_control_poll_event`]. Bounded; `try_send` drops on overflow rather
/// than blocking the RX pump (only matters during the non-yielding boot phase).
static EVENTS: Channel<CriticalSectionRawMutex, DelugeInputEvent, 64> = Channel::new();

/// Background task: run the PIC baud-rate handshake, then decode its byte stream
/// into [`EVENTS`]. Spawned by `main` before the app tick loop; makes progress
/// whenever the app yields. OLED chip-select bytes are forwarded to the pic
/// notifier (for the display path, M2c); encoder motion never arrives via the PIC
/// (it is wired to RZ/A1L GPIO IRQs — see [`deluge_encoder_take_edges`]).
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
                // Latch the raw version byte for `deluge_control_read_boot_info`.
                // High bit (0x80) = OLED present; low 7 bits = version (mirrors the
                // rza1 BSP's control_surface.cpp decode). Mark "received" with 0x100.
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

/// PIC pad id → grid (x,y). Matches `deluge_bsp::pic::pad_coords` and
/// src/bsp/rza1/control_surface.cpp (`Pad(uint8_t)` in the app's pad.cpp).
fn pad_event(id: u8, value: i16) -> DelugeInputEvent {
    let (x, y) = pic::pad_coords(id);
    DelugeInputEvent {
        kind: KIND_PAD,
        x,
        y,
        value,
    }
}

/// PIC button id (raw − 144) → matrix (x,y) = (id % 9, id / 9). Matches the rza1
/// BSP's `(raw % 9, raw / 9 − kDisplayHeight*2)` with raw = id + 144 (= id + 16·9).
fn button_event(id: u8, value: i16) -> DelugeInputEvent {
    DelugeInputEvent {
        kind: KIND_BUTTON,
        x: id % 9,
        y: id / 9,
        value,
    }
}

/// Report PIC boot info to the app's one-time boot sequence (deluge.cpp): PIC
/// firmware version, OLED-present, and whether the user held the reset control at
/// power-on. Mirrors the rza1 BSP's `control_surface.cpp` decode of the version
/// byte (low 7 bits = version, high bit = OLED present).
///
/// `factory_reset_requested` is ALWAYS false here: the embassy PIC parser does not
/// decode the boot reset-burst, so there is no genuine signal — and the previous
/// STUB left this (and the whole struct) UNINITIALIZED, so the app read stack
/// garbage and triggered a spurious "FACTORY RESET" (clobbering settings) on most
/// boots. Returning a definite false is both correct and what stops that.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_read_boot_info(out: *mut DelugeBootInfo) {
    let fw = BOOT_FW.load(Ordering::Relaxed);
    let (version, oled_present) = if fw & BOOT_FW_RECEIVED != 0 {
        ((fw & 0x7f) as u8, (fw & 0x80) != 0)
    } else {
        // Version byte not captured in time (boot race): the app targets the OLED
        // panel on this BSP, so default present. Strictly better than the prior
        // garbage; `factory_reset_requested` below is the load-bearing fix.
        (0, true)
    };
    // SAFETY: the app passes a valid DelugeBootInfo out-pointer (sync boot call).
    unsafe {
        (*out).pic_firmware_version = version;
        (*out).oled_present = oled_present;
        (*out).factory_reset_requested = false;
    }
}

/// Drain one decoded input event into `*out`. Returns false when none buffered.
/// The app calls this repeatedly each tick (readButtonsAndPads).
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

/// Map the app's encoder ordinal (hid::encoders::EncoderName: SCROLL_Y, SCROLL_X,
/// TEMPO, SELECT, MOD_1, MOD_0) to deluge_bsp::encoder's ENCODER_DELTAS index
/// (a different physical/pin order). Derived by matching pins between
/// src/bsp/rza1/encoder_io.c (app order) and deluge_bsp encoder::irq_init SETUP:
///   app 0 SCROLL_Y(8/10)→4, 1 SCROLL_X(11/12)→0, 2 TEMPO(6/7)→1,
///   3 SELECT(3/2)→5, 4 MOD_1(5/4)→3, 5 MOD_0(0/15)→2. Invert flags line up.
const ENC_REMAP: [usize; encoder::NUM_ENCODERS] = [4, 0, 1, 5, 3, 2];

/// Atomically read and clear the accumulated signed A-pin edge count for
/// `encoder` (raw edges; the app applies its own detent policy). The counts are
/// accumulated in deluge_bsp::encoder's quadrature ISRs (in its own pin order,
/// remapped here to the app's ordinal).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_encoder_take_edges(encoder: u8) -> i8 {
    let i = encoder as usize;
    if i >= encoder::NUM_ENCODERS {
        return 0;
    }
    encoder::ENCODER_DELTAS[ENC_REMAP[i]].swap(0, Ordering::Relaxed)
}

// ── Control-surface OUTPUT (LEDs / pads) ───────────────────────────────────────
//
// All PIC *transmit* goes through ONE task ([`pad_render`]) draining this command
// queue. This is deliberate: the PIC TX path holds an async mutex (DMA_TX_LOCK)
// across each transfer, so we must never `block_on` a PIC call from a tick while
// the render task might hold that lock — that would deadlock (block_on stalls the
// executor, so the task can't resume to release it). A single TX owner + a
// command queue keeps every send on the async side, and the task `.await`s
// between sends so the RX pump still runs. The boundary fns are non-blocking
// producers (try_send; drop on overflow — a dropped frame self-heals next redraw).

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

/// Max bytes one queued command becomes on the PIC wire (a column-pair =
/// 1 + 16·3 = 49). Used to report [`deluge_control_pad_output_space`] in *bytes*,
/// which is what the app's redraw gates compare against (kNumBytesIn* in
/// definitions_cxx.hpp: col update = 49, main redraw = 392, sidebar = 49).
const BYTES_PER_CMD: u32 = 49;

/// Output command queue. Sized so a full grid + sidebar redraw (≈10–18 commands)
/// leaves plenty of free slots — important because the app gates redraws on
/// [`deluge_control_pad_output_space`], which we derive from the free slot count.
static OUT: Channel<CriticalSectionRawMutex, PicOut, 64> = Channel::new();
/// Set by any output op; [`deluge_control_flush`] turns a dirty state into a
/// single Refresh so we don't spam done_sending_rows on every (mostly idle) tick.
static OUTPUT_DIRTY: AtomicBool = AtomicBool::new(false);

/// Background task: the sole PIC transmitter. Drains [`OUT`] and performs each
/// send asynchronously, yielding between them so the RX pump and the app tick
/// loop keep running.
#[embassy_executor::task]
pub async fn pad_render() {
    // Don't transmit until pic::init() has finished the baud handshake — sending
    // a command mid-handshake would go out at the wrong rate. Commands queued by
    // the app during boot wait in OUT until then.
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
    // Drop on overflow rather than block the app tick; a dropped pad frame
    // self-heals on the next redraw.
    let _ = OUT.try_send(cmd);
    OUTPUT_DIRTY.store(true, Ordering::Relaxed);
}

/// Set the colour of one main-grid pad. `host_app`-only: the app's real pad
/// rendering path batches whole columns (see [`deluge_control_set_pad_columns`],
/// implemented for both device and host above), so this single-pad entry point
/// is unreached in practice. A no-op keeps host-linked boot-and-idle working.
#[cfg(all(not(target_os = "none"), feature = "host_app"))]
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_pad(_x: u8, _y: u8, _colour: DelugeColour) {}

/// Set the colours of a main-grid column pair. `colours` holds `count` RGB
/// entries (one per LED in the pair; the Deluge sends 16). The app pre-packs the
/// PIC wire layout, so we forward the bytes as-is.
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

/// Begin a horizontal scroll animation on the surface.
///
/// The PIC animates a scroll from its own off-screen framebuffer: the app calls this once, then
/// feeds one colour per row per tick via [`deluge_control_scroll_row`], ending each tick with
/// [`deluge_control_scroll_done`]. That is why the scroll path does NOT go through
/// [`deluge_control_set_pad_columns`] — sending one colour per row rather than the whole grid every
/// ~7 ms is the point of the protocol.
///
/// `flags`: bit 0 = scrolling in the positive direction, bit 1 = the scrolled area includes the
/// sidebar (18 columns, not 16). Forwarded as-is; see the SDK's opcode docs for why the encoding is
/// arithmetic rather than named constants.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_horizontal(flags: u8) {
    enqueue(PicOut::ScrollSetup(flags));
}

/// Supply the incoming colour for one row of an in-progress horizontal scroll (`row` 0-7).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_row(row: u8, colour: DelugeColour) {
    enqueue(PicOut::ScrollRow {
        row,
        colour: [colour.r, colour.g, colour.b],
    });
}

/// End a scroll tick: tells the PIC the row data for this step is complete, which is what makes it
/// refresh. Shares the Refresh command with a normal grid update.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_scroll_done() {
    enqueue(PicOut::Refresh);
}

/// Perform a vertical scroll, supplying the whole incoming row. `colours` holds `count` entries, one
/// per column (the app sends all 18).
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

/// Set an indicator LED on/off (board-defined index).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_led(led: u8, on: bool) {
    enqueue(PicOut::Led { id: led, on });
}

/// Set a gold-knob/mod indicator ring. `which` selects the knob; `levels` points
/// to `count` per-LED brightnesses (the Deluge ring has 4).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_indicator(which: u8, levels: *const u8, count: u8) {
    let mut ring = [0u8; 4];
    let n = (count as usize).min(4);
    // SAFETY: the app passes `count` valid brightness bytes.
    let src = unsafe { core::slice::from_raw_parts(levels, n) };
    ring[..n].copy_from_slice(&src[..n]);
    // rza1 BSP maps `which != 0` to the knob select.
    enqueue(PicOut::Indicator {
        knob: (which != 0) as u8,
        levels: ring,
    });
}

/// Surface refresh interval (board units).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_refresh_time(ms: u8) {
    let _ = OUT.try_send(PicOut::RefreshTime(ms));
}

/// Free space, in BYTES, for queued pad output — the app gates redraws on this
/// (`> kNumBytesInMainPadRedraw + …`), so it must be a byte count, not a slot
/// count. We map free queue slots → bytes via [`BYTES_PER_CMD`]; this gives real
/// back-pressure (drops as the queue fills) while clearing the redraw gates when
/// the queue is reasonably empty.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_pad_output_space() -> u32 {
    OUT.free_capacity() as u32 * BYTES_PER_CMD
}

/// Flush batched surface output: if anything was queued since the last flush,
/// append a single display-refresh. Called every tick, so gate on the dirty flag
/// to avoid spamming refreshes when nothing changed.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flush() {
    if OUTPUT_DIRTY.swap(false, Ordering::Relaxed) {
        let _ = OUT.try_send(PicOut::Refresh);
    }
}

/// Scheduler task id of the app's encoder task (`interpretEncodersTask`), captured
/// from [`deluge_encoder_io_init`]. -1 until `encoders::init()` runs. The
/// [`encoder_wake_pump`] task uses it to unblock the encoder task on movement.
static ENCODER_WAKE_TASK: AtomicI8 = AtomicI8::new(-1);

/// Set up encoder GPIO/IRQs. The actual GPIO+GIC bring-up runs in `main`'s
/// pre-executor window (encoder::irq_init, which must precede interrupt enable),
/// so this boundary call — invoked later from encoders::init() — just records the
/// `wake_task` id so [`encoder_wake_pump`] can wake the app's encoder task.
///
/// The app's `interpretEncodersTask` self-blocks (`blockTask`) after each run and
/// relies on the board to `unblockTask` it when an encoder moves (the original
/// rza1 TaskManager contract). Without that wake the task runs once and parks
/// forever — i.e. the encoders go dead. (On the legacy `deluge_app_tick` superloop
/// this was masked by a per-tick `interpretEncoders()` call; the decomposed
/// scheduler has no such loop, so the wake must be delivered here.)
#[unsafe(no_mangle)]
pub extern "C" fn deluge_encoder_io_init(wake_task: i8) {
    ENCODER_WAKE_TASK.store(wake_task, Ordering::Relaxed);
    log::info!("deluge-rust: deluge_encoder_io_init (encoder wake task id={wake_task})");
}

/// Bridge the board's encoder edge ISRs to the scheduler: the quadrature ISRs
/// accumulate into `encoder::ENCODER_DELTAS` and fire `encoder::ENCODER_WAKER`;
/// this task waits on that waker and `unblockTask`s the app's self-blocked encoder
/// task so it drains the edges (and re-blocks). Without this, encoder input never
/// reaches the app on the decomposed scheduler. Spawned by `main`.
#[embassy_executor::task]
pub async fn encoder_wake_pump() {
    loop {
        // Park until an edge ISR signals movement. Register the waker *before*
        // checking the deltas so an ISR landing in the gap can't be missed.
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
        // Bound the unblock rate (and let the encoder task drain the deltas before
        // we re-check, else we'd spin until it runs). ~1 ms matches the app's
        // former encoder-task cadence — imperceptible for knob turns.
        embassy_time::Timer::after(embassy_time::Duration::from_millis(1)).await;
    }
}
