//! `libdeluge/audio_io.h` — duplex block audio over the free-running SSI0 DMA rings.
//!
//! Board audio bring-up starts both DMA rings, which `deluge_bsp::audio` holds; this module owns
//! only the read/write cursors and the pacing, and fills and reads the rings by offset (the
//! rings' positional window). The app renders i32 stereo via `deluge_app_render`, written directly
//! into the uncached TX ring.
//!
//! No ISR touches this state. The `static mut` cursors belong to the render (the preemptive audio
//! tier) alone, apart from `deluge_audio_start`, which anchors them before the render starts. The
//! cross-tier exceptions are atomics: [`RENDER_BLOCKS`], bumped by the render and read by
//! [`deluge_audio_await_block`] in thread mode; [`BLOCK_START_PLAY`], written by the render and read
//! by the task tier to place capture stamps; and [`RX_RESYNC_REQUESTED`], which the task tier sets
//! for the render to act on.
#![allow(non_upper_case_globals)]
// The unsafe ops inside the unsafe fns are justified per site.
#![allow(unsafe_op_in_unsafe_fn)]

use core::sync::atomic::{AtomicBool, AtomicPtr, AtomicU32, AtomicUsize, Ordering};

use deluge_bsp::audio;

use crate::audio::pacing::pace_window;
use crate::audio::ring::{frame_at, read_frame, write_frame};
use crate::sys::{DelugeStatus, DelugeStatus_DELUGE_OK as DELUGE_OK, DelugeStereoSample};

/// TX ring length in stereo frames (a power of two, for the masks).
const TX_FRAMES: usize = audio::SSI_TX_WORDS / 2; // 1024
/// RX ring length in stereo frames.
const RX_FRAMES: usize = audio::SSI_RX_WORDS / 2; // 2048
pub(crate) const TX_MASK: usize = TX_FRAMES - 1;
pub(crate) const RX_MASK: usize = RX_FRAMES - 1;

/// The codec rate, in frames per second.
pub(crate) const SAMPLE_RATE_HZ: u32 = 44_100;

/// Initial RX read latency: the read cursor trails the RX DMA write head by about one TX ring
/// plus a margin, so a render never reads uncaptured frames.
const RX_LATENCY_FRAMES: usize = RX_FRAMES - TX_FRAMES - 16;

/// Maximum frames per `deluge_app_render` call.
///
/// Must not exceed the app's render-buffer size (`SSI_TX_BUFFER_NUM_SAMPLES` = 128 in
/// `board_config.h`): the app renders into fixed arrays of that size, and a larger block writes
/// out of bounds into adjacent globals. The TX ring is larger and independent.
const APP_BLOCK_FRAMES: usize = 128;

const ZERO: DelugeStereoSample = DelugeStereoSample { l: 0, r: 0 };

// Render scratch, sized to the app's render-buffer limit. In SDRAM (`.sdram_bss`, zeroed at boot)
// to spare internal SRAM; RENDER_BLOCK is flushed into the SRAM TX ring.
#[unsafe(link_section = ".sdram_bss")]
static mut RENDER_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];
#[unsafe(link_section = ".sdram_bss")]
static mut INPUT_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];

// Cursors, in stereo frames.
static mut TX_WRITE: usize = 0; // write cursor (frame index into TX ring)
static mut PLAY_CACHED: usize = 0; // last-polled DMA play head (frame index)
static mut RX_READ: usize = 0; // read cursor (frame index into RX ring)
static mut PENDING_POS: usize = 0; // [PENDING_POS, PENDING_END) = rendered, not yet flushed
static mut PENDING_END: usize = 0;
// Anchors of the current render block, for sample-accurate gate/MIDI timing.
static mut BLOCK_START_WRITE: usize = 0;
/// The play frame at which the most recently begun render block started.
///
/// Written by the render ([`deluge_audio_drive`], audio tier) and read by the task tier, so it is
/// atomic.
static BLOCK_START_PLAY: AtomicUsize = AtomicUsize::new(0);

/// How many render blocks have begun: bumped by the render (audio tier) just before each
/// `deluge_app_render` call, read by [`deluge_audio_await_block`]. Wraps; only a change matters.
static RENDER_BLOCKS: AtomicU32 = AtomicU32::new(0);

/// How long [`deluge_audio_await_block`] waits for a block before returning anyway, so a stopped
/// render cannot hang its caller. Several blocks' worth (one is ~2.9 ms at 128 frames).
const AWAIT_BLOCK_BOUND_MS: u64 = 20;

/// The TX and RX rings' first words (uncached alias), cached the first time each is asked for once
/// board audio init has lent them; null before. The rings never move or go away after init.
static RING_TX: AtomicPtr<u32> = AtomicPtr::new(core::ptr::null_mut());
static RING_RX: AtomicPtr<u32> = AtomicPtr::new(core::ptr::null_mut());

/// The TX ring's first word, or null before board audio init.
fn tx_ring() -> *mut u32 {
    let p = RING_TX.load(Ordering::Acquire);
    if !p.is_null() {
        return p;
    }
    let p = audio::with_tx(|t| t.ring_mut().as_mut_ptr()).unwrap_or(core::ptr::null_mut());
    RING_TX.store(p, Ordering::Release);
    p
}

/// The RX ring's first word, or null before board audio init.
fn rx_ring() -> *const u32 {
    let p = RING_RX.load(Ordering::Acquire);
    if !p.is_null() {
        return p;
    }
    let p = audio::with_rx(|r| r.ring().as_ptr() as *mut u32).unwrap_or(core::ptr::null_mut());
    RING_RX.store(p, Ordering::Release);
    p
}

/// Returns the DMA TX play head as a frame index into the ring; 0 before board audio init.
#[inline]
pub(crate) fn tx_play_frame() -> usize {
    frame_at(audio::with_tx(|t| t.position()), TX_MASK)
}

/// Returns the DMA RX write head as a frame index into the ring; 0 before board audio init.
///
/// The RX DMA runs on the codec clock from board audio init, so this is also the clock USB audio
/// paces its packets by.
#[inline]
pub(crate) fn rx_write_frame() -> usize {
    frame_at(audio::with_rx(|r| r.position()), RX_MASK)
}

/// How many frames the codec plays before it reaches the block being rendered. Audio tier, inside
/// the render.
pub(crate) fn block_lead_frames() -> usize {
    // SAFETY: `BLOCK_START_WRITE` is the render's, and this runs inside it.
    let write = unsafe { BLOCK_START_WRITE };
    write.wrapping_sub(BLOCK_START_PLAY.load(Ordering::Relaxed)) & TX_MASK
}

/// Flushes pending rendered frames into the TX ring; returns true once all are flushed, false if
/// the write cursor would lap the play head. Re-polls the play head once when the ring looks full.
unsafe fn flush_pending() -> bool {
    let base = tx_ring();
    let rb = core::ptr::addr_of!(RENDER_BLOCK) as *const DelugeStereoSample;
    while PENDING_POS != PENDING_END {
        // A zero lead means the write cursor is a full ring ahead of the play head.
        if TX_WRITE.wrapping_sub(PLAY_CACHED) & TX_MASK == 0 {
            PLAY_CACHED = tx_play_frame();
            if TX_WRITE.wrapping_sub(PLAY_CACHED) & TX_MASK == 0 {
                return false;
            }
        }
        let s = *rb.add(PENDING_POS);
        // SAFETY: TX_WRITE is masked into [0, TX_FRAMES), within the ring's TX_FRAMES frames.
        write_frame(base, TX_WRITE, s.l, s.r);
        TX_WRITE = (TX_WRITE + 1) & TX_MASK;
        PENDING_POS += 1;
    }
    true
}

/// Copies the next `frames` received frames from the RX ring into `INPUT_BLOCK` and advances the
/// read cursor.
unsafe fn prepare_input_block(frames: usize) {
    let base = rx_ring();
    let ib = core::ptr::addr_of_mut!(INPUT_BLOCK) as *mut DelugeStereoSample;
    for i in 0..frames {
        let fp = (RX_READ + i) & RX_MASK;
        // SAFETY: fp masked into [0, RX_FRAMES), within the ring's RX_FRAMES frames.
        let (l, r) = read_frame(base, fp);
        *ib.add(i) = DelugeStereoSample { l, r };
    }
    RX_READ = (RX_READ + frames) & RX_MASK;
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_start() -> DelugeStatus {
    // The DMA stream free-runs from board audio init; only (re-)anchor the cursors. Idempotent.
    unsafe {
        TX_WRITE = 0;
        RX_READ = RX_LATENCY_FRAMES & RX_MASK;
        PLAY_CACHED = tx_play_frame();
        PENDING_POS = 0;
        PENDING_END = 0;
    }
    DELUGE_OK
}

/// Set by [`deluge_audio_input_resync`] on the task tier; the render performs the resync, since
/// the read cursor is the render's.
static RX_RESYNC_REQUESTED: AtomicBool = AtomicBool::new(false);

/// Pulls the RX read cursor forward until input latency is in `[TX_FRAMES, 2 * TX_FRAMES)`. The RX
/// ring is deeper than the TX ring, and a glitch or startup can leave latency a full TX ring too
/// high.
///
/// # Safety
///
/// Only the render may call this: it writes `RX_READ`.
unsafe fn resync_rx() {
    let rx_write = rx_write_frame();
    loop {
        let lat = rx_write.wrapping_sub(RX_READ).wrapping_sub(TX_FRAMES) & RX_MASK;
        if lat < TX_FRAMES {
            break;
        }
        RX_READ = (RX_READ + TX_FRAMES) & RX_MASK;
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_drive() -> u32 {
    let mut renders = 0u32;
    unsafe {
        if RX_RESYNC_REQUESTED.swap(false, Ordering::Acquire) {
            resync_rx();
        }
        while flush_pending() && renders < 2 {
            PLAY_CACHED = tx_play_frame();
            // Frames the play head is ahead of the write cursor = free space.
            let raw = PLAY_CACHED.wrapping_sub(TX_WRITE) & TX_MASK;
            // Render only as far as the play head advanced; a second pass in one drive needs more
            // than 10 free frames per render already done.
            if raw as u32 <= 10 * renders {
                break;
            }
            // Cap at the app's render-buffer size, not the TX ring (see APP_BLOCK_FRAMES).
            let frames = pace_window(raw, APP_BLOCK_FRAMES);
            BLOCK_START_WRITE = TX_WRITE;
            BLOCK_START_PLAY.store(PLAY_CACHED, Ordering::Relaxed);
            prepare_input_block(frames);
            RENDER_BLOCKS.fetch_add(1, Ordering::Release);
            crate::sys::deluge_app_render(
                core::ptr::addr_of!(INPUT_BLOCK) as *const DelugeStereoSample,
                core::ptr::addr_of_mut!(RENDER_BLOCK) as *mut DelugeStereoSample,
                frames as u32,
            );
            PENDING_POS = 0;
            PENDING_END = frames;
            renders += 1;
        }
    }
    renders
}

/// Waits (in WFI) until the render begins its next block, or [`AWAIT_BLOCK_BOUND_MS`] passes.
///
/// The render runs on the preemptive audio executor, so rendering from thread mode would run a
/// second render alongside it. The bound lets a stopped render return control to the caller's own
/// deadline.
///
/// Nothing arms an alarm for the deadline: the bound relies on some interrupt waking the WFI. The
/// render task's `Timer` keeps one queued, so the core wakes at least once a block.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_await_block() {
    let start = RENDER_BLOCKS.load(Ordering::Acquire);
    let deadline =
        embassy_time::Instant::now() + embassy_time::Duration::from_millis(AWAIT_BLOCK_BOUND_MS);
    while RENDER_BLOCKS.load(Ordering::Acquire) == start && embassy_time::Instant::now() < deadline
    {
        // SAFETY: WFI only idles the core until the next interrupt; it touches no memory.
        unsafe { core::arch::asm!("wfi", options(nostack, preserves_flags)) };
    }
}

/// Asks the render to resync the input read cursor at its next drive (see `resync_rx`). The caller
/// is on the task tier and the cursor is the render's, so this only sets a request.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_input_resync() {
    RX_RESYNC_REQUESTED.store(true, Ordering::Release);
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_frames_until_block_offset(
    offset_frames: u32,
    elapsed_frames_out: *mut u32,
) -> u32 {
    let play_now = tx_play_frame();
    unsafe {
        if !elapsed_frames_out.is_null() {
            let block_start_play = BLOCK_START_PLAY.load(Ordering::Relaxed);
            *elapsed_frames_out = (play_now.wrapping_sub(block_start_play) & TX_MASK) as u32;
        }
        (BLOCK_START_WRITE
            .wrapping_add(offset_frames as usize)
            .wrapping_sub(play_now)
            & TX_MASK) as u32
    }
}

/// Returns how far past the start of the most recently begun render block the event stamped
/// `stamp` fell, in frames (see [`capture_stamp`](super::capture_stamp)). Task tier.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_stamp_to_render_offset(stamp: u32) -> u32 {
    let block_start = BLOCK_START_PLAY.load(Ordering::Relaxed) as u32;
    crate::audio::capture_stamp::render_offset(stamp, block_start, TX_MASK as u32)
}

abi_pin!(
    deluge_audio_await_block,
    deluge_audio_drive,
    deluge_audio_frames_until_block_offset,
    deluge_audio_input_resync,
    deluge_audio_stamp_to_render_offset,
    deluge_audio_start,
);
