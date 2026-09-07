//! display.h — main OLED output over `deluge_bsp::oled`.
//!
//! The app renders a 128×48, 1-bpp, page-major framebuffer (768 bytes, the
//! SSD1309 wire layout) and hands it to [`deluge_display_blit_oled`]. We stash it
//! and signal [`oled_render`] — the sole owner of the panel — which runs the
//! SSD1309 init once, then streams frames over the shared RSPI0 bus (the PIC
//! drives chip-select / D-C; deluge_bsp::oled handles that handshake + DMA).
//! Async transfer behind the synchronous boundary; the app's tick yields, so the
//! render task makes progress.
//!
//! The app's framebuffer layout matches `oled::FrameBuffer` exactly (6 pages ×
//! 128 cols, bit 0 = top row of a page), so the blit is a flat 768-byte copy.

use core::sync::atomic::{AtomicBool, Ordering};

use deluge_bsp::{oled, pic};

use crate::sys::{DelugeStatus, DelugeStatus_DELUGE_OK as DELUGE_OK};

/// Latest frame from the app, awaiting transfer. Written by [`deluge_display_blit_oled`]
/// (synchronously, from the app tick), read by [`oled_render`]. Safe without a
/// lock: the executor is cooperative and single-core, and neither the blit copy
/// nor the render copy `.await`s mid-copy, so they cannot interleave; no ISR
/// touches it.
static mut FRAME: [u8; oled::FRAME_BYTES] = [0; oled::FRAME_BYTES];
/// Set once the app has blitted at least one frame (else render a blank panel).
static FRAME_VALID: AtomicBool = AtomicBool::new(false);
/// True while a frame transfer is in flight (`deluge_display_busy`).
static BUSY: AtomicBool = AtomicBool::new(false);

/// OLED render task: the sole owner of the panel. Waits for the PIC handshake,
/// runs the SSD1309 init, then re-streams the latest app frame on each redraw.
#[embassy_executor::task]
pub async fn oled_render() {
    // oled::init() issues PIC commands (DC/enable/select) — wait for pic::init()
    // to finish its baud handshake first, like the PIC output task.
    pic::wait_ready().await;
    oled::init().await;
    log::info!("deluge-rust: OLED initialised");

    let mut fb = oled::FrameBuffer::new();
    // Show a blank panel immediately so it's not powered up with garbage.
    fb.fill(0x00);
    oled::send_frame(&fb).await;

    loop {
        oled::wait_redraw().await;
        if FRAME_VALID.load(Ordering::Acquire) {
            // SAFETY: see FRAME's note — no concurrent access under the
            // cooperative executor; the copy does not await.
            fb.pages
                .as_flattened_mut()
                .copy_from_slice(unsafe { &*core::ptr::addr_of!(FRAME) });
        }
        BUSY.store(true, Ordering::Release);
        oled::send_frame(&fb).await;
        BUSY.store(false, Ordering::Release);
    }
}

/// Blit a 1-bpp OLED framebuffer. `pixels` is `width*height/8` bytes in the
/// panel's page-major layout. Stores the frame and signals the render task;
/// returns immediately (the transfer is async).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_blit_oled(
    pixels: *const u8,
    width: u16,
    height: u16,
) -> DelugeStatus {
    let n = (width as usize * height as usize / 8).min(oled::FRAME_BYTES);
    // SAFETY: the app passes `n` valid framebuffer bytes; FRAME is only touched
    // here and in oled_render, which cannot run concurrently with this copy.
    unsafe {
        let src = core::slice::from_raw_parts(pixels, n);
        let dst = &mut *core::ptr::addr_of_mut!(FRAME);
        dst[..n].copy_from_slice(src);
    }
    FRAME_VALID.store(true, Ordering::Release);
    oled::notify_redraw();
    DELUGE_OK
}

/// True while a previous transfer is still in flight.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_busy() -> bool {
    BUSY.load(Ordering::Acquire)
}

/// One-time display bring-up. The render task owns the SSD1309 init sequence
/// (it must run async, after the PIC handshake), so there's nothing to do here.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_init() {
    log::info!("deluge-rust: deluge_display_init (OLED init runs in oled_render task)");
}

/// Service pending display output. The render task pushes transfers autonomously
/// whenever the app yields, so there's no app-driven transfer state machine here.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_service() {}

/// Display service-timer tick — nothing to advance (autonomous render task).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_timer_event() {}

/// Synchronously blit a framebuffer and show it from a frozen (fault) context,
/// bypassing the async queue, then BLOCK until the user presses the select knob.
///
/// The blocking half is not decoration — it is the whole meaning of "freeze". This used
/// to draw and return, which quietly turned every `FREEZE_WITH_ERROR` on this BSP into
/// "print a code and carry on", so an assertion proceeded into exactly the corruption it
/// was placed there to prevent. Observed doing precisely that: an E199
/// ("voice being freed is not in this Sound's list") drew its error screen and then ran
/// on into `freeActiveVoice` for that stale voice, ending in a wild branch to 0x20.
/// The legacy BSP's implementation blocks the same way (`display_freeze.cpp`), so this
/// restores the shared contract rather than inventing one.
///
/// Polls the PIC receive path directly rather than awaiting [`crate::control`]'s queue:
/// callers are mid-assertion with the world deliberately stopped, and on the fault-vector
/// path there is no executor left to service an `async` read.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_freeze(pixels: *const u8) {
    /// PIC byte for a select-knob press — the resume gesture, matching the legacy BSP.
    const PIC_SELECT_KNOB_PRESS: u8 = 175;

    let mut fb = oled::FrameBuffer::new();
    // SAFETY: the fault handler passes a full-framebuffer pointer.
    unsafe {
        let src = core::slice::from_raw_parts(pixels, oled::FRAME_BYTES);
        fb.pages.as_flattened_mut().copy_from_slice(src);
        // draw_blocking is a no-op until oled::init() has run (checks INITIALISED).
        oled::draw_blocking(&fb);
    }

    // Hold here until the user acknowledges. The PIC's receive DMA is hardware and keeps
    // filling its ring with no interrupt or task needed, so polling it works even from a
    // fault context.
    #[cfg(target_os = "none")]
    loop {
        // SAFETY: reads the PIC channel's DMA receive ring; `uart::init_dma_rx` ran at
        // boot, and a read is side-effect-free beyond consuming the byte.
        if let Some(byte) = unsafe { rza1l_hal::uart::try_read_dma(deluge_bsp::uart::PIC_CH) } {
            if byte == PIC_SELECT_KNOB_PRESS {
                return;
            }
        }
        core::hint::spin_loop();
    }

    // Host builds (the golden renderer, the Embassy host harness) have no PIC and no user, so
    // there is no acknowledgement to wait for — blocking would hang a batch render instead of
    // stopping a device in front of someone. Log it, THEN abort the process.
    //
    // The log line alone is not a gate: every A/B script here builds this crate `--release`, and
    // `deluge-bsp`/`rza1l-hal` request `log`'s `release_max_level_off` feature for the device
    // build's benefit — Cargo unifies that feature across the whole binary's dependency graph, so
    // in a `--release` host build `log::error!` (like every other `log::*!` call) compiles to a
    // no-op. Grepping this process's output for the message would therefore find nothing whether
    // or not a freeze happened. The abort is what makes a `FREEZE_WITH_ERROR` on host observable
    // at all: it restores the pre-freeze behaviour where the same call was a live `assert()` that
    // `abort()`ed (host builds have no `NDEBUG`), which is what the song-preview/audio-load A/B
    // scripts' existing "leg exited nonzero" checks already fail a run on — see
    // `deluge_system_reset` in services.rs for the same host-abort pattern.
    #[cfg(not(target_os = "none"))]
    {
        let _ = PIC_SELECT_KNOB_PRESS;
        log::error!("deluge-rust: display freeze (assertion) — no PIC on this build, aborting");
        std::process::abort();
    }
}
