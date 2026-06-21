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
pub extern "C" fn deluge_display_blit_oled(pixels: *const u8, width: u16, height: u16) -> DelugeStatus {
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
/// bypassing the async queue. `pixels` is the full 768-byte framebuffer.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_freeze(pixels: *const u8) {
    let mut fb = oled::FrameBuffer::new();
    // SAFETY: the fault handler passes a full-framebuffer pointer.
    unsafe {
        let src = core::slice::from_raw_parts(pixels, oled::FRAME_BYTES);
        fb.pages.as_flattened_mut().copy_from_slice(src);
        // draw_blocking is a no-op until oled::init() has run (checks INITIALISED).
        oled::draw_blocking(&fb);
    }
}
