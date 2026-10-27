//! `libdeluge/control_surface.h` and `libdeluge/encoder_io.h` — pads and buttons (via the PIC),
//! LEDs, and encoders.
//!
//! [`pic_pump`] decodes the PIC byte stream into `EVENTS`; [`pad_render`] is the sole PIC
//! transmitter. The synchronous boundary functions only queue and drain. The one-time UART and
//! encoder GPIO/GIC setup runs in the device runtime's pre-executor window, with IRQs masked.
#![allow(non_snake_case)]

#[cfg(not(target_os = "none"))]
use core::sync::atomic::AtomicU32;
use core::sync::atomic::{AtomicBool, AtomicU16, Ordering};

use deluge_bsp::{encoder, pic};
use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::channel::Channel;
use embassy_sync::signal::Signal;

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
/// arrives via the PIC; it comes from the encoder pins (see [`deluge_encoder_take_quadrature_edges`]).
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
            queue_event(ev);
        }
    }
}

/// Queues `ev` for the app and wakes its loop with `WAKE_INPUT`; drops it when [`EVENTS`] is full.
///
/// The wake is the only way the driver hears of input: it has no level check on [`EVENTS`].
pub fn queue_event(ev: DelugeInputEvent) {
    if EVENTS.try_send(ev).is_ok() {
        crate::app_loop::wake(crate::app_loop::WAKE_INPUT);
    }
}

/// Queues `ev` without waking the app's loop; returns false when [`EVENTS`] is full. A host test
/// seam: it stages a burst that the driver hears of only through one later wake.
#[cfg(not(target_os = "none"))]
#[doc(hidden)]
pub fn queue_event_without_wake(ev: DelugeInputEvent) -> bool {
    EVENTS.try_send(ev).is_ok()
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

/// Atomically reads and clears the signed quadrature-edge count decoded for `encoder` (an app
/// ordinal), four edges per cycle. Raw edges: the app applies its own detent policy.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_encoder_take_quadrature_edges(encoder: u8) -> i8 {
    let i = encoder as usize;
    if i >= encoder::NUM_ENCODERS {
        return 0;
    }
    encoder::ENCODER_DELTAS[ENC_REMAP[i]].swap(0, Ordering::Relaxed)
}

/// Adds `edges` to the app's encoder `index` (its ordinal, as `deluge_encoder_take_quadrature_edges` takes
/// it), saturating at the accumulator's range, and wakes the app's encoder wait.
#[cfg(not(target_os = "none"))]
pub fn add_encoder_edges_for_harness(index: u8, edges: i32) {
    let i = index as usize;
    if i >= encoder::NUM_ENCODERS {
        return;
    }
    let add = edges.clamp(i8::MIN as i32, i8::MAX as i32) as i8;
    let _ = encoder::ENCODER_DELTAS[ENC_REMAP[i]].try_update(
        Ordering::Relaxed,
        Ordering::Relaxed,
        |v| Some(v.saturating_add(add)),
    );
    encoder::ENCODER_WAKER.wake();
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
    /// Flash pad `idx` (`y + x * 8`), in the PIC's colour index `colour` (1–3), or its current
    /// flash colour when 0.
    Flash { idx: u8, colour: u8 },
    /// The PIC's pad-dimming interval.
    DimmerInterval(u8),
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
/// Set by any output op; [`pad_render`] clears it with a single refresh once `OUT` drains, so an
/// idle batch sends nothing extra.
static OUTPUT_DIRTY: AtomicBool = AtomicBool::new(false);

/// Set while the OLED task runs a frame: its PIC select, the transfer, and its PIC deselect.
///
/// The OLED holds RSPI0, which CV output shares, from before its select until the PIC echoes the
/// deselect. Both commands go out on the PIC UART, which takes no turns, so a pad redraw in
/// progress could hold them back for as long as the app keeps queueing pad output, and CV with
/// them. While this is set [`pad_render`] sends nothing and [`deluge_control_pad_output_space`]
/// reports none, so the app holds its redraws and retries them.
static OLED_FRAME: AtomicBool = AtomicBool::new(false);
/// Wakes [`pad_render`] when an OLED frame ends.
static OLED_FRAME_DONE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// Gives the PIC UART to an OLED frame until [`end_oled_frame`].
///
/// Yields once first, so pad output woken by the last frame's end sends a command before this
/// frame takes the UART: back-to-back frames still let pad output through.
pub async fn begin_oled_frame() {
    embassy_futures::yield_now().await;
    OLED_FRAME.store(true, Ordering::Release);
}

/// Hands the PIC UART back to pad output after [`begin_oled_frame`].
pub fn end_oled_frame() {
    OLED_FRAME.store(false, Ordering::Release);
    OLED_FRAME_DONE.signal(());
}

/// Suspends while an OLED frame holds the PIC UART.
async fn wait_out_oled_frame() {
    while OLED_FRAME.load(Ordering::Acquire) {
        OLED_FRAME_DONE.wait().await;
    }
}

/// Counts [`refresh`] calls, so a host test can check pad_render sent exactly one PIC refresh
/// after a batch drained. The host stand-in for the PIC UART (`pic::tx`) only logs, so this is the
/// one place that call is otherwise observable off-target.
#[cfg(not(target_os = "none"))]
static REFRESH_COUNT: AtomicU32 = AtomicU32::new(0);

/// Pad flashes and dimmer intervals [`send`] has handed to the PIC, so a host test can check they
/// reach it (the host stand-in for the PIC UART only logs).
#[cfg(not(target_os = "none"))]
static FLASHES_SENT: AtomicU32 = AtomicU32::new(0);
#[cfg(not(target_os = "none"))]
static DIMMERS_SENT: AtomicU32 = AtomicU32::new(0);

/// Host only: (pad flashes, dimmer intervals) sent to the PIC so far.
#[cfg(not(target_os = "none"))]
pub fn flash_and_dimmer_sent_for_test() -> (u32, u32) {
    (
        FLASHES_SENT.load(Ordering::Relaxed),
        DIMMERS_SENT.load(Ordering::Relaxed),
    )
}

/// Current value of [`REFRESH_COUNT`]. Host test seam.
#[cfg(not(target_os = "none"))]
#[doc(hidden)]
pub fn refresh_count_for_test() -> u32 {
    REFRESH_COUNT.load(Ordering::Relaxed)
}

/// Resets [`REFRESH_COUNT`] to 0. Host test seam.
#[cfg(not(target_os = "none"))]
#[doc(hidden)]
pub fn reset_refresh_count_for_test() {
    REFRESH_COUNT.store(0, Ordering::Relaxed);
}

/// Background task: the sole PIC transmitter. Drains `OUT`, yielding between sends so the RX pump
/// and the app keep running.
///
/// Every [`enqueue`] runs synchronously inside the app job that called it, and this task runs on
/// the same thread executor, so once `OUT` is found empty that job's whole batch has been sent —
/// exactly the moment to trigger the PIC's refresh, which [`refresh`] does once per batch rather
/// than once per command. The refresh is sent from here, after the queue is known empty, so it
/// cannot be dropped by a full queue the way a `try_send` from the app's side could be.
#[embassy_executor::task]
pub async fn pad_render() {
    // A command sent mid-handshake would go out at the wrong baud rate; boot-time commands wait
    // in OUT.
    pic::wait_ready().await;
    loop {
        send(OUT.receive().await).await;
        while let Ok(cmd) = OUT.try_receive() {
            send(cmd).await;
        }
        if OUTPUT_DIRTY.swap(false, Ordering::Relaxed) {
            refresh().await;
        }
    }
}

/// Sends one queued control-surface output operation to the PIC, once no OLED frame holds it.
async fn send(cmd: PicOut) {
    wait_out_oled_frame().await;
    match cmd {
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
        PicOut::Flash { idx, colour } => {
            if colour > 0 {
                pic::flash_pad_colour(idx, colour).await
            } else {
                pic::flash_pad(idx).await
            }
            #[cfg(not(target_os = "none"))]
            FLASHES_SENT.fetch_add(1, Ordering::Relaxed);
        }
        PicOut::DimmerInterval(interval) => {
            pic::set_dimmer_interval(interval).await;
            #[cfg(not(target_os = "none"))]
            DIMMERS_SENT.fetch_add(1, Ordering::Relaxed);
        }
        PicOut::Refresh => refresh().await,
        PicOut::ScrollSetup(flags) => pic::setup_horizontal_scroll(flags).await,
        PicOut::ScrollRow { row, colour } => pic::send_scroll_row(row, colour).await,
        PicOut::ScrollVertical { up, colours, count } => {
            pic::vertical_scroll(up, &colours[..(count as usize).min(18)]).await
        }
    }
}

/// Sends the PIC's "done sending rows" command, which triggers its display refresh. The sole
/// place that happens, so [`REFRESH_COUNT`] on host counts every one.
async fn refresh() {
    wait_out_oled_frame().await;
    pic::done_sending_rows().await;
    #[cfg(not(target_os = "none"))]
    REFRESH_COUNT.fetch_add(1, Ordering::Relaxed);
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
    #[cfg(not(target_os = "none"))]
    crate::peripherals::ui_harness::record_pad_columns(idx, src);
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
    #[cfg(not(target_os = "none"))]
    crate::peripherals::ui_harness::record_led(led, on);
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
    #[cfg(not(target_os = "none"))]
    crate::peripherals::ui_harness::record_ring(which, &ring[..n]);
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

/// Flashes pad `idx` (`y + x * 8`) in the PIC's current flash colour. The PIC times the flash and
/// restores the pad itself, so no refresh follows.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad(idx: u8) {
    let _ = OUT.try_send(PicOut::Flash { idx, colour: 0 });
}

/// Flashes pad `idx` (`y + x * 8`) in the PIC's colour index `colour_idx` (1–3).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flash_pad_colour(idx: u8, colour_idx: i32) {
    let colour = u8::try_from(colour_idx).unwrap_or(0);
    let _ = OUT.try_send(PicOut::Flash { idx, colour });
}

/// Sets the PIC's pad-dimming interval.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_set_dimmer_interval(interval: u8) {
    let _ = OUT.try_send(PicOut::DimmerInterval(interval));
}

/// Returns the free space for queued pad output, in bytes: free queue slots × `BYTES_PER_CMD`, or
/// none while an OLED frame holds the PIC UART (see [`OLED_FRAME`]).
///
/// The app gates redraws on this against byte counts, so it must not be a slot count.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_pad_output_space() -> u32 {
    if OLED_FRAME.load(Ordering::Acquire) {
        return 0;
    }
    OUT.free_capacity() as u32 * BYTES_PER_CMD
}

/// No-op: [`pad_render`] sends the PIC refresh when its output queue drains, so the app has
/// nothing to flush. The ABI function exists because the app calls it after pad/LED bursts.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_control_flush() {}

abi_pin!(
    deluge_control_flash_pad,
    deluge_control_flash_pad_colour,
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
    deluge_control_set_dimmer_interval,
    deluge_control_set_pad_columns,
    deluge_control_set_refresh_time,
    deluge_encoder_take_quadrature_edges,
);
