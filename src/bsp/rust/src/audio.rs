//! audio_io.h — duplex block audio over the free-running SSI0 DMA rings.
//!
//! Direct port of `src/bsp/rza1/audio_io.cpp`. The SSI TX/RX DMA rings free-run
//! from board audio bring-up (`deluge_bsp::audio::init` → `ssi::init`, which
//! starts both DMA channels and enables TX+RX). This module owns only the
//! read/write cursors and the trickle pacing: the app renders i32 stereo via
//! `deluge_app_render(in, out, frames)`, and we write those samples *directly*
//! into the uncached TX ring (no f32 round-trip — the app is i32-native).
//!
//! All state is touched only from the app's single task (deluge_audio_drive runs
//! inside deluge_app_tick; the timing queries from the same app context), and no
//! ISR touches it, so the `static mut` access is single-threaded.
#![allow(non_upper_case_globals)]
// This module is a single-threaded port (state touched only from the app task,
// no ISR); the unsafe ops inside the unsafe fns are documented per-site.
#![allow(unsafe_op_in_unsafe_fn)]

use rza1l_hal::ssi;

use crate::sys::{DelugeStatus, DelugeStatus_DELUGE_OK as DELUGE_OK, DelugeStereoSample};

unsafe extern "C" {
    /// The app renders `frames` stereo samples into `out`, reading `frames`
    /// aligned input samples from `in` (app.h). i32-native; no scaling here.
    fn deluge_app_render(input: *const DelugeStereoSample, out: *mut DelugeStereoSample, frames: u32);
}

/// TX ring length in stereo frames (must be a power of two for the masks).
const TX_FRAMES: usize = ssi::TX_FRAMES; // 1024
/// RX ring length in stereo frames.
const RX_FRAMES: usize = ssi::RX_FRAMES; // 2048
const TX_MASK: usize = TX_FRAMES - 1;
const RX_MASK: usize = RX_FRAMES - 1;

/// Initial RX read latency: keep the read cursor ~one TX-ring + a margin behind
/// the RX DMA write head (mirrors rza1 audio_io.cpp's start anchor).
const RX_LATENCY_FRAMES: usize = RX_FRAMES - TX_FRAMES - 16;

/// Maximum frames per `deluge_app_render` call. This MUST NOT exceed the app's
/// internal render-buffer size (board_config.h SSI_TX_BUFFER_NUM_SAMPLES = 128):
/// the app renders into fixed `renderingMemory`/`reverbMemory` arrays of this
/// size, so a larger block makes it write out of bounds into adjacent globals
/// (it was clobbering AudioEngine::reverb's vtable → wild call). Our TX DMA ring
/// (TX_FRAMES) is larger and independent; we just trickle ≤128-frame renders
/// into it.
const APP_BLOCK_FRAMES: usize = 128;

const ZERO: DelugeStereoSample = DelugeStereoSample { l: 0, r: 0 };

// Render scratch, sized to the app's render-buffer limit. Placed in SDRAM
// (.sdram_bss, zeroed by boot) to spare the tight internal SRAM; the app renders
// into RENDER_BLOCK and reads INPUT_BLOCK, then we trickle RENDER_BLOCK into the
// (SRAM) TX DMA ring.
#[unsafe(link_section = ".sdram_bss")]
static mut RENDER_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];
#[unsafe(link_section = ".sdram_bss")]
static mut INPUT_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];

// Cursors, all in stereo-frame units.
static mut TX_WRITE: usize = 0; // write cursor (frame index into TX ring)
static mut PLAY_CACHED: usize = 0; // last-polled DMA play head (frame index)
static mut RX_READ: usize = 0; // read cursor (frame index into RX ring)
static mut PENDING_POS: usize = 0; // [PENDING_POS, PENDING_END) = rendered, not yet flushed
static mut PENDING_END: usize = 0;
// Anchors of the current render block, for sample-accurate gate/MIDI timing.
static mut BLOCK_START_WRITE: usize = 0;
static mut BLOCK_START_PLAY: usize = 0;

/// DMA TX play head as a frame index into the ring.
#[inline]
fn tx_play_frame() -> usize {
    // SAFETY: both pointers are into the same TX ring (uncached alias).
    let slots = unsafe { ssi::tx_current_ptr().offset_from(ssi::tx_buf_start()) } as usize;
    (slots / 2) & TX_MASK
}

/// DMA RX write head as a frame index into the ring.
#[inline]
fn rx_write_frame() -> usize {
    // SAFETY: both pointers are into the same RX ring (uncached alias).
    let slots = unsafe { ssi::rx_current_ptr().offset_from(ssi::rx_buf_start()) } as usize;
    (slots / 2) & RX_MASK
}

/// Flush pending rendered frames into the TX ring, stopping (returns false) when
/// the write cursor would lap the play head; returns true once all are flushed.
/// Re-polls the play head once when it appears full (as `doSomeOutputting()` did).
unsafe fn flush_pending() -> bool {
    let base = ssi::tx_buf_start();
    let rb = core::ptr::addr_of!(RENDER_BLOCK) as *const DelugeStereoSample;
    while PENDING_POS != PENDING_END {
        // Lead == 0 means the write cursor is a full ring ahead of the play head
        // (ring full) — re-poll once, and if still full, stop for now.
        if TX_WRITE.wrapping_sub(PLAY_CACHED) & TX_MASK == 0 {
            PLAY_CACHED = tx_play_frame();
            if TX_WRITE.wrapping_sub(PLAY_CACHED) & TX_MASK == 0 {
                return false;
            }
        }
        let s = *rb.add(PENDING_POS);
        // SAFETY: TX_WRITE is masked into [0, TX_FRAMES); 2 slots per frame.
        let p = base.add(TX_WRITE * 2);
        p.write_volatile(s.l);
        p.add(1).write_volatile(s.r);
        TX_WRITE = (TX_WRITE + 1) & TX_MASK;
        PENDING_POS += 1;
    }
    true
}

/// Copy the next `frames` received frames out of the RX ring into INPUT_BLOCK and
/// advance the read cursor (wrapping).
unsafe fn prepare_input_block(frames: usize) {
    let base = ssi::rx_buf_start();
    let ib = core::ptr::addr_of_mut!(INPUT_BLOCK) as *mut DelugeStereoSample;
    for i in 0..frames {
        let fp = (RX_READ + i) & RX_MASK;
        // SAFETY: fp masked into [0, RX_FRAMES); 2 slots per frame; uncached alias.
        let p = base.add(fp * 2);
        *ib.add(i) = DelugeStereoSample {
            l: p.read_volatile(),
            r: p.add(1).read_volatile(),
        };
    }
    RX_READ = (RX_READ + frames) & RX_MASK;
}

/// Round/expand a raw free-frame count into a render window (audio_pacing.h):
/// double the amount over a small threshold (smoother under light load), cap at
/// the ring, then round up to a multiple of 4 for the NEON kernels.
fn pace_window(raw: usize, ring: usize) -> usize {
    const THRESHOLD: i32 = 6;
    let mut n = raw as i32;
    if (n as usize) < ring {
        let over = n - THRESHOLD;
        if over > 0 {
            n = THRESHOLD + (over << 1);
        }
    }
    if (n as usize) > ring {
        n = ring as i32;
    }
    if n >= 3 {
        n = (n + 2) & !3;
    }
    n as usize
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_start() -> DelugeStatus {
    // The SSI/DMA stream free-runs from board audio init; just (re-)anchor the
    // cursors. Idempotent.
    unsafe {
        TX_WRITE = 0;
        RX_READ = RX_LATENCY_FRAMES & RX_MASK;
        PLAY_CACHED = tx_play_frame();
        PENDING_POS = 0;
        PENDING_END = 0;
    }
    DELUGE_OK
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_stop() {
    // The free-running ring keeps the codec alive; nothing to do on this board.
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_max_block_frames() -> u32 {
    APP_BLOCK_FRAMES as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_sample_rate() -> u32 {
    44_100
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_output_latency_frames() -> u32 {
    // The app renders up to one TX ring ahead of the play head.
    TX_FRAMES as u32
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_drive() -> u32 {
    let mut renders = 0u32;
    unsafe {
        while flush_pending() && renders < 2 {
            PLAY_CACHED = tx_play_frame();
            // Frames the play head is ahead of the write cursor = free space.
            let raw = PLAY_CACHED.wrapping_sub(TX_WRITE) & TX_MASK;
            // Render only as far as the play head advanced; on a second pass
            // within one drive, only if it's been long enough to be worth it.
            if raw as u32 <= 10 * renders {
                break;
            }
            // Cap at the app's render-buffer size (NOT the TX ring) — see APP_BLOCK_FRAMES.
            let frames = pace_window(raw, APP_BLOCK_FRAMES);
            BLOCK_START_WRITE = TX_WRITE;
            BLOCK_START_PLAY = PLAY_CACHED;
            prepare_input_block(frames);
            deluge_app_render(
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

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_input_resync() {
    // The RX ring is deeper than the TX ring and timing comes from the TX play
    // head; a glitch/startup can leave RX latency a full TX-ring too high. Pull
    // the read cursor forward until latency is within [TX_FRAMES, 2*TX_FRAMES).
    unsafe {
        let rx_write = rx_write_frame();
        loop {
            let lat = rx_write.wrapping_sub(RX_READ).wrapping_sub(TX_FRAMES) & RX_MASK;
            if lat < TX_FRAMES {
                break;
            }
            RX_READ = (RX_READ + TX_FRAMES) & RX_MASK;
        }
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_frames_until_block_offset(
    offset_frames: u32,
    elapsed_frames_out: *mut u32,
) -> u32 {
    let play_now = tx_play_frame();
    unsafe {
        if !elapsed_frames_out.is_null() {
            *elapsed_frames_out = (play_now.wrapping_sub(BLOCK_START_PLAY) & TX_MASK) as u32;
        }
        (BLOCK_START_WRITE.wrapping_add(offset_frames as usize).wrapping_sub(play_now) & TX_MASK)
            as u32
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_stamp_to_render_offset(stamp: u32) -> u32 {
    // `stamp` is a board capture tick → frame offset on the render timeline.
    // M5 (MIDI/gate) refines this; the masked delta is the M3 placeholder.
    unsafe { ((stamp as usize).wrapping_sub(TX_WRITE) & TX_MASK) as u32 }
}
