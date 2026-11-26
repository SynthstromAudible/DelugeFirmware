//! `libdeluge/audio_io.h` — duplex block audio over the free-running SSI0 DMA rings.
//!
//! Board audio bring-up starts both DMA rings, which `deluge_bsp::audio` holds; this module owns
//! the block drive and the input read cursor, and fills and reads the rings by offset (the rings'
//! positional window). The drive renders fixed [`BLOCK_FRAMES`]-frame blocks, keeping
//! [`QUEUE_BLOCKS`] of them written ahead of the block the codec plays ([`block_drive`] decides
//! which); the render task wakes it as each TX block completes. The app renders i32 stereo via
//! `deluge_app_render` into a scratch block, copied into the uncached TX ring.
//!
//! No ISR touches this state. The `static mut` state belongs to the render (the preemptive audio
//! tier) alone, apart from `deluge_audio_start`, which anchors it before the render starts. The
//! cross-tier exceptions are atomics: [`RENDER_BLOCKS`], bumped by the render and read by
//! [`deluge_audio_await_block`] in thread mode; [`BLOCK_START_PLAY`], [`PLAYED_BLOCK`],
//! [`WRITE_END`] and [`OVERRUNS`], written by the render and read by the task tier (the middle two
//! place capture stamps on the play timeline); and [`RX_RESYNC_REQUESTED`], which the task tier
//! sets for the render to act on.
//!
//! [`block_drive`]: crate::audio::block_drive
#![allow(non_upper_case_globals)]
// The unsafe ops inside the unsafe fns are justified per site.
#![allow(unsafe_op_in_unsafe_fn)]

use core::sync::atomic::{AtomicBool, AtomicPtr, AtomicU32, AtomicUsize, Ordering};

use deluge_bsp::audio;

use embassy_time::Instant;

use crate::audio::block_drive::{BLOCK_FRAMES, BlockDrive, QUEUE_BLOCKS, RING_BLOCKS};
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

// The block is the app's render-buffer size (`SSI_TX_BUFFER_NUM_SAMPLES` in `board_config.h`): the
// app renders into fixed arrays of that size, and a larger block writes out of bounds into adjacent
// globals. The TX ring is whole blocks.
const _: () = assert!(BLOCK_FRAMES == 128 && TX_FRAMES == RING_BLOCKS * BLOCK_FRAMES);

const ZERO: DelugeStereoSample = DelugeStereoSample { l: 0, r: 0 };

// Render scratch, sized to the app's render-buffer limit. In SDRAM (`.sdram_bss`, zeroed at boot)
// to spare internal SRAM; RENDER_BLOCK is flushed into the SRAM TX ring.
#[unsafe(link_section = ".sdram_bss")]
static mut RENDER_BLOCK: [DelugeStereoSample; BLOCK_FRAMES] = [ZERO; BLOCK_FRAMES];
#[unsafe(link_section = ".sdram_bss")]
static mut INPUT_BLOCK: [DelugeStereoSample; BLOCK_FRAMES] = [ZERO; BLOCK_FRAMES];

/// Which blocks to render, and the overruns.
static mut DRIVE: BlockDrive = BlockDrive::new();
/// When the drive last planned, for its lap check; `None` before the first plan.
static mut LAST_PLAN: Option<Instant> = None;
/// The RX read cursor, a frame index into the RX ring.
static mut RX_READ: usize = 0;
/// The TX frame the current render block starts at, for sample-accurate gate/MIDI timing.
static mut BLOCK_START_WRITE: usize = 0;
/// The play frame at which the most recently begun render block started.
///
/// Written by the render ([`deluge_audio_drive`], audio tier) and read by the task tier, so it is
/// atomic.
static BLOCK_START_PLAY: AtomicUsize = AtomicUsize::new(0);

/// The play-timeline frame (see [`capture_stamp`](crate::audio::capture_stamp)) the next rendered
/// sample plays at: just past the most recently rendered block. Written by the render after each
/// block, read by the task tier to place input events.
static WRITE_END: AtomicU32 = AtomicU32::new(0);
/// The drive's unwrapped play block at its last plan, as a wrapping `u32`: the base the play
/// timeline counts the DMA position on from. Written by the render, read by any tier stamping an
/// input event.
static PLAYED_BLOCK: AtomicU32 = AtomicU32::new(0);

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
    let p = audio::with_tx(|t| t.as_mut_ptr()).unwrap_or(core::ptr::null_mut());
    RING_TX.store(p, Ordering::Release);
    p
}

/// The RX ring's first word, or null before board audio init.
fn rx_ring() -> *const u32 {
    let p = RING_RX.load(Ordering::Acquire);
    if !p.is_null() {
        return p;
    }
    let p = audio::with_rx(|r| r.as_ptr() as *mut u32).unwrap_or(core::ptr::null_mut());
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

/// Copies the rendered block into the TX ring from frame `start`, a block boundary. The drive plans
/// only blocks ahead of the one playing, so the codec is not reading them.
unsafe fn write_block(start: usize) {
    let base = tx_ring();
    let rb = core::ptr::addr_of!(RENDER_BLOCK) as *const DelugeStereoSample;
    for i in 0..BLOCK_FRAMES {
        let s = *rb.add(i);
        // SAFETY: `start` is a block boundary inside the ring and the ring is whole blocks, so
        // `start + i` is below TX_FRAMES.
        write_frame(base, start + i, s.l, s.r);
    }
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
    // The DMA stream free-runs from board audio init; only (re-)anchor the drive and the input
    // cursor. Idempotent.
    unsafe {
        (*core::ptr::addr_of_mut!(DRIVE)).restart();
        RX_READ = RX_LATENCY_FRAMES & RX_MASK;
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

/// Renders the blocks the drive plans for the play head's position now: one per completed TX block
/// in steady state, up to [`QUEUE_BLOCKS`] after a late wake. Returns how many it rendered.
///
/// An overrun (the codec reached a block not yet written) is counted in [`OVERRUNS`] and skips the
/// lost blocks, output and input alike, so the queue restarts one block ahead of the play head. A
/// block the codec started while it was still being written counts too.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_drive() -> u32 {
    let mut renders = 0u32;
    unsafe {
        if RX_RESYNC_REQUESTED.swap(false, Ordering::Acquire) {
            resync_rx();
        }
        let now = Instant::now();
        let elapsed_us = LAST_PLAN.map_or(0, |t| (now - t).as_micros());
        LAST_PLAN = Some(now);
        let drive = &mut *core::ptr::addr_of_mut!(DRIVE);
        let plan = drive.plan(tx_play_frame(), elapsed_us);
        PLAYED_BLOCK.store(drive.played_block() as u32, Ordering::Relaxed);
        OVERRUNS.store(drive.overruns(), Ordering::Relaxed);
        if plan.resynced {
            let skipped = (plan.skipped % (RX_FRAMES / BLOCK_FRAMES) as u64) as usize;
            RX_READ = (RX_READ + skipped * BLOCK_FRAMES) & RX_MASK;
            resync_rx();
        }
        for i in 0..plan.renders as usize {
            let play = tx_play_frame();
            BLOCK_START_WRITE = ((plan.write_block + i) % RING_BLOCKS) * BLOCK_FRAMES;
            BLOCK_START_PLAY.store(play, Ordering::Relaxed);
            prepare_input_block(BLOCK_FRAMES);
            RENDER_BLOCKS.fetch_add(1, Ordering::Release);
            #[cfg(feature = "audio-timing")]
            let probe_start = Instant::now();
            crate::sys::deluge_app_render(
                core::ptr::addr_of!(INPUT_BLOCK) as *const DelugeStereoSample,
                core::ptr::addr_of_mut!(RENDER_BLOCK) as *mut DelugeStereoSample,
                BLOCK_FRAMES as u32,
            );
            write_block(BLOCK_START_WRITE);
            drive.rendered();
            WRITE_END.store(
                (drive.written_blocks() as u32).wrapping_mul(BLOCK_FRAMES as u32),
                Ordering::Relaxed,
            );
            if drive.check_written(tx_play_frame()) {
                OVERRUNS.store(drive.overruns(), Ordering::Relaxed);
            }
            #[cfg(feature = "audio-timing")]
            probe::record_render(
                BLOCK_FRAMES as u32,
                // The frames queued ahead of the codec when this render began: the blocks written
                // after the one playing, less what the codec has already played of its own.
                (BLOCK_START_WRITE.wrapping_sub(play) & TX_MASK) as u32,
                (Instant::now() - probe_start).as_micros() as u32,
                (tx_play_frame().wrapping_sub(play) & TX_MASK) as u32,
                drive.overruns(),
            );
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
/// TX DMA's block interrupt wakes the core once a block.
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

/// Returns how many samples after the next one the render produces the event stamped `stamp`
/// takes effect at, for a constant latency from the event (see
/// [`input_offset`](super::capture_stamp::input_offset)). Task tier.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_stamp_to_render_offset(stamp: u32) -> u32 {
    crate::audio::capture_stamp::input_offset(stamp, WRITE_END.load(Ordering::Relaxed))
}

/// The play-timeline frame the DMA is at now (see [`capture_stamp`](crate::audio::capture_stamp)):
/// what an input event is stamped with. Any tier.
pub(crate) fn play_timeline_frame() -> u32 {
    crate::audio::capture_stamp::timeline_frame(
        tx_play_frame() as u32,
        PLAYED_BLOCK.load(Ordering::Relaxed),
        TX_MASK as u32,
    )
}

/// Overruns the drive has counted: the render updates it after each plan, so the task tier reads it
/// without touching the drive's state.
static OVERRUNS: AtomicU32 = AtomicU32::new(0);

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_block_frames() -> u32 {
    BLOCK_FRAMES as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_queue_blocks() -> u32 {
    QUEUE_BLOCKS as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_overruns() -> u32 {
    OVERRUNS.load(Ordering::Relaxed)
}

abi_pin!(
    deluge_audio_await_block,
    deluge_audio_block_frames,
    deluge_audio_drive,
    deluge_audio_overruns,
    deluge_audio_queue_blocks,
    deluge_audio_frames_until_block_offset,
    deluge_audio_input_resync,
    deluge_audio_stamp_to_render_offset,
    deluge_audio_start,
);

/// The audio-timing probe's interval, kept by the render (the only writer) and printed every
/// two seconds of audio.
#[cfg(feature = "audio-timing")]
mod probe {
    use crate::audio::timing_probe::TimingStats;

    static mut STATS: TimingStats = TimingStats::EMPTY;
    /// The drive's overrun count at the last render recorded.
    static mut SEEN_OVERRUNS: u32 = 0;

    fn stats() -> &'static mut TimingStats {
        // SAFETY: only the audio tier touches STATS (the render and the render task, which never
        // run at once), so no two references are live together.
        unsafe { &mut *core::ptr::addr_of_mut!(STATS) }
    }

    /// A render this long is reported at once, on its own `AUDIO-SLOW` line: a trace can stop on it.
    const SLOW_US: u32 = 10_000;

    pub(super) fn record_render(frames: u32, queued: u32, us: u32, played: u32, overruns: u32) {
        if us >= SLOW_US {
            log::info!(
                "AUDIO-SLOW us={} frames={} queued={} played={}",
                us,
                frames,
                queued,
                played
            );
        }
        let s = stats();
        s.record_render(frames, queued, us, played);
        // SAFETY: as STATS.
        s.note_drive_overruns(overruns, unsafe {
            &mut *core::ptr::addr_of_mut!(SEEN_OVERRUNS)
        });
        if s.interval_done() {
            log::info!("{}", s);
            log::info!("{}", crate::peripherals::usb_audio::take_service_stats());
            *s = TimingStats::EMPTY;
        }
    }

    pub(super) fn record_wake_late(late_us: u32) {
        stats().record_wake_late(late_us);
    }
}

/// Notes how late the render task woke after its timer's deadline (the audio-timing probe).
#[cfg(feature = "audio-timing")]
pub(crate) fn note_wake_late(late_us: u32) {
    probe::record_wake_late(late_us);
}
