//! `libdeluge/display.h` — the main OLED, over `deluge_bsp::oled`.
//!
//! The app hands a 128×48 1-bpp framebuffer to [`deluge_display_blit_oled`], which stashes it and
//! signals [`oled_render`], the sole owner of the panel. The render task streams frames over the
//! shared RSPI0 bus asynchronously, making progress whenever the app yields.
//!
//! The app's layout is the SSD1309 wire layout, `oled::FrameBuffer`'s (6 pages × 128 columns,
//! bit 0 = a page's top row), so the blit is a flat 768-byte copy.

use core::sync::atomic::{AtomicBool, Ordering};

use deluge_bsp::{oled, pic};

use crate::sys::{
    DelugeStatus, DelugeStatus_DELUGE_ERR_PARAM as DELUGE_ERR_PARAM,
    DelugeStatus_DELUGE_OK as DELUGE_OK,
};

/// The latest frame from the app, written by [`deluge_display_blit_oled`] and read by
/// [`oled_render`].
///
/// Needs no lock: both run on the same single-core cooperative executor, neither copy awaits, and
/// no ISR touches it.
static mut FRAME: [u8; oled::FRAME_BYTES] = [0; oled::FRAME_BYTES];
/// Set once the app has blitted a frame; until then the panel shows blank.
static FRAME_VALID: AtomicBool = AtomicBool::new(false);

/// Background task: the sole owner of the panel. Waits for the PIC handshake, runs the SSD1309
/// init, then streams the latest app frame on each redraw.
#[embassy_executor::task]
pub async fn oled_render() {
    // oled::init() issues PIC commands, so the baud handshake must finish first.
    pic::wait_ready().await;
    oled::init().await;
    log::info!("deluge-rust: OLED initialised");

    let mut fb = oled::FrameBuffer::new();
    // Show a blank panel at once rather than power-up garbage.
    fb.fill(0x00);
    oled::send_frame(&fb).await;

    loop {
        oled::wait_redraw().await;
        if FRAME_VALID.load(Ordering::Acquire) {
            // SAFETY: no concurrent access to FRAME (see its doc); the copy does not await.
            fb.pages
                .as_flattened_mut()
                .copy_from_slice(unsafe { &*core::ptr::addr_of!(FRAME) });
        }
        oled::send_frame(&fb).await;
    }
}

/// Stores a 1-bpp framebuffer of `width * height / 8` bytes (page-major) and signals the render
/// task; returns without waiting for the transfer.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_blit_oled(
    pixels: *const u8,
    width: u16,
    height: u16,
) -> DelugeStatus {
    let n = (width as usize * height as usize / 8).min(oled::FRAME_BYTES);
    // SAFETY: the app passes `n` valid framebuffer bytes; FRAME is only touched
    // here and in oled_render, which cannot run concurrently with this copy.
    let src = unsafe { core::slice::from_raw_parts(pixels, n) };
    // SAFETY: as above, nothing else touches FRAME during this copy.
    unsafe { (&mut *core::ptr::addr_of_mut!(FRAME))[..n].copy_from_slice(src) };
    #[cfg(not(target_os = "none"))]
    crate::peripherals::ui_harness::record_oled(src, width, height);
    FRAME_VALID.store(true, Ordering::Release);
    oled::notify_redraw();
    DELUGE_OK
}

/// Blits to panel `index`. The Deluge has one panel: index 0 is [`deluge_display_blit_oled`], any
/// other index is `DELUGE_ERR_PARAM`.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_blit_oled_n(
    index: u8,
    pixels: *const u8,
    width: u16,
    height: u16,
) -> DelugeStatus {
    if index != 0 {
        return DELUGE_ERR_PARAM;
    }
    deluge_display_blit_oled(pixels, width, height)
}

/// Whether panel `index` is mid-transfer. Never: a blit only stashes the frame for the render task,
/// so a new one can always be handed over.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_busy_n(_index: u8) -> bool {
    false
}

/// One-time display bring-up: nothing to do, since [`oled_render`] runs the panel init
/// asynchronously after the PIC handshake.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_init() {
    log::info!("deluge-rust: deluge_display_init (OLED init runs in oled_render task)");
}

/// Services pending display output: nothing to do, since [`oled_render`] transfers on its own.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_service() {}

/// Display service-timer tick: nothing to advance.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_timer_event() {}

/// Draws a framebuffer synchronously, bypassing the render task, then blocks until the user
/// presses the select knob. On host, aborts the process instead.
///
/// The block is the point of `FREEZE_WITH_ERROR`: returning would let a failed assertion run on
/// into the corruption it guards against.
///
/// Polls the PIC receive ring directly rather than awaiting [`control`](super::control)'s queue:
/// callers are mid-assertion, and on the fault-vector path no executor is running.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_display_freeze(pixels: *const u8) {
    /// PIC byte for a select-knob press — the resume gesture.
    const PIC_SELECT_KNOB_PRESS: u8 = 175;
    /// PIC prefix byte marking the following button code as a release, not a press.
    #[cfg_attr(not(target_os = "none"), allow(dead_code))]
    const PIC_NEXT_PAD_OFF: u8 = 252;

    let mut fb = oled::FrameBuffer::new();
    // SAFETY: the fault handler passes a full-framebuffer pointer.
    unsafe {
        let src = core::slice::from_raw_parts(pixels, oled::FRAME_BYTES);
        fb.pages.as_flattened_mut().copy_from_slice(src);
        // draw_blocking is a no-op until oled::init() has run (checks INITIALISED).
        oled::draw_blocking(&fb);
    }

    // The PIC's receive DMA keeps filling its ring with no interrupt or task, so polling it works
    // from a fault context. Releases carry the press's code after PIC_NEXT_PAD_OFF; skipping them
    // keeps a freeze triggered by a select press from being dismissed by its own release.
    #[cfg(target_os = "none")]
    {
        // SAFETY: a freeze runs with the thread executor blocked in this call, from an
        // interrupt-priority executor (the audio render on SGI 8) that preempted it, or from a fault
        // with interrupts masked. Either way the PIC task (suspended mid-read, holding its
        // receiver) can't read the ring meanwhile, and its `try_read` masks interrupts across its
        // update, so it can't be preempted halfway. The stolen receiver shares its read position,
        // so the bytes read here, the resume press included, don't reach the app afterwards.
        let mut pic_rx = unsafe { deluge_bsp::uart::steal_pic_rx() };
        let mut next_is_release = false;
        loop {
            if let Some(byte) = pic_rx.as_mut().and_then(|rx| rx.try_read()) {
                if byte == PIC_NEXT_PAD_OFF {
                    next_is_release = true;
                    continue;
                }
                if byte == PIC_SELECT_KNOB_PRESS && !next_is_release {
                    return;
                }
                next_is_release = false;
            }
            core::hint::spin_loop();
        }
    }

    // Host builds have no PIC and no user; blocking would hang a batch render. The abort is what
    // makes a freeze observable: `deluge-bsp`/`rza1l-hal` enable `log`'s `release_max_level_off`,
    // so in a `--release` host build this log line compiles away, and the harnesses detect the
    // freeze by the nonzero exit.
    #[cfg(not(target_os = "none"))]
    {
        let _ = PIC_SELECT_KNOB_PRESS;
        log::error!("deluge-rust: display freeze (assertion) — no PIC on this build, aborting");
        std::process::abort();
    }
}

abi_pin!(
    deluge_display_blit_oled,
    deluge_display_blit_oled_n,
    deluge_display_busy_n,
    deluge_display_freeze,
    deluge_display_init,
    deluge_display_service,
    deluge_display_timer_event,
);
