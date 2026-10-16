//! The UI harness's view of the surface, on host builds: what the app last sent to the OLED, the LEDs,
//! the pads and the knob rings, kept so a scripted run can be snapshotted, and a door that puts a
//! script's gestures on the same input path a player's take. `tests/harness/level3/host_script.c` is
//! the only caller of the C ABI here; its header declares it.

use core::sync::atomic::Ordering;
use std::sync::Mutex;

use deluge_bsp::encoder;

use crate::peripherals::control;
use crate::sys::{DelugeColour, DelugeInputEvent, DelugeInputEventKind};

/// Pad columns the shadow holds (the Deluge uses 18: 16 main + 2 sidebar).
pub const PAD_COLS: usize = 32;
/// Pad rows.
pub const PAD_ROWS: usize = 8;
/// Indicator LEDs the shadow holds.
pub const LED_COUNT: usize = 64;
/// Gold-knob indicator rings.
pub const RINGS: usize = 2;
/// Levels per ring.
pub const RING_LEVELS: usize = 4;
/// Bytes of the largest panel frame the shadow holds.
pub const OLED_BYTES: usize = 1024;
/// The Deluge's main grid plus its sidebar, in columns, and its rows.
const GRID_COLS: u8 = 18;
const GRID_ROWS: u8 = 8;

struct Shadows {
    oled: [u8; OLED_BYTES],
    oled_size: (u16, u16),
    pads: [u8; PAD_COLS * PAD_ROWS * 3],
    leds: [u8; LED_COUNT],
    rings: [[u8; RING_LEVELS]; RINGS],
    ring_written: [bool; RINGS],
}

static SHADOWS: Mutex<Shadows> = Mutex::new(Shadows {
    oled: [0; OLED_BYTES],
    oled_size: (0, 0),
    pads: [0; PAD_COLS * PAD_ROWS * 3],
    leds: [0; LED_COUNT],
    rings: [[0; RING_LEVELS]; RINGS],
    ring_written: [false; RINGS],
});

fn shadows() -> std::sync::MutexGuard<'static, Shadows> {
    SHADOWS.lock().unwrap_or_else(|e| e.into_inner())
}

/// Records an indicator LED.
pub fn record_led(led: u8, on: bool) {
    if let Some(slot) = shadows().leds.get_mut(led as usize) {
        *slot = on as u8;
    }
}

/// Records a knob ring; missing levels read 0.
pub fn record_ring(which: u8, levels: &[u8]) {
    let mut s = shadows();
    let i = (which != 0) as usize;
    s.rings[i] = [0; RING_LEVELS];
    for (dst, src) in s.rings[i].iter_mut().zip(levels) {
        *dst = *src;
    }
    s.ring_written[i] = true;
}

/// Records a main-grid column pair: `colours[0..8]` are column `2 * idx`, rows 0..7, and `colours[8..16]`
/// column `2 * idx + 1`.
pub fn record_pad_columns(idx: u8, colours: &[DelugeColour]) {
    let mut s = shadows();
    for (i, c) in colours.iter().enumerate().take(16) {
        let col = idx as usize * 2 + i / 8;
        let row = i % 8;
        if col < PAD_COLS {
            let at = (col * PAD_ROWS + row) * 3;
            s.pads[at..at + 3].copy_from_slice(&[c.r, c.g, c.b]);
        }
    }
}

/// Records the frame blitted to panel 0; a frame larger than the shadow is refused.
pub fn record_oled(pixels: &[u8], width: u16, height: u16) {
    if pixels.is_empty() || pixels.len() > OLED_BYTES {
        return;
    }
    let mut s = shadows();
    s.oled[..pixels.len()].copy_from_slice(pixels);
    s.oled_size = (width, height);
}

/// Clears every shadow. Host test seam.
#[doc(hidden)]
pub fn reset_for_test() {
    let mut s = shadows();
    s.oled_size = (0, 0);
    s.pads = [0; PAD_COLS * PAD_ROWS * 3];
    s.leds = [0; LED_COUNT];
    s.ring_written = [false; RINGS];
    while control::deluge_control_poll_event(&mut DelugeInputEvent {
        kind: 0,
        x: 0,
        y: 0,
        value: 0,
    }) {}
    for d in &encoder::ENCODER_DELTAS {
        d.store(0, Ordering::Relaxed);
    }
}

/// Queues a script's pad, button or "no presses" event on the app's input queue.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_push_event(kind: DelugeInputEventKind, x: u8, y: u8, value: i16) {
    control::queue_event(DelugeInputEvent { kind, x, y, value });
}

/// Adds `edges` to the app's encoder `index` (its own ordinal), saturating, and wakes the app's encoder wait.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_add_encoder_edges(index: u8, edges: i32) {
    control::add_encoder_edges_for_harness(index, edges);
}

/// Sets the panel set a `board` step asks for. Only the Deluge's one panel exists here: any spec is refused.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_set_displays(_spec: *const core::ffi::c_char) -> i32 {
    -1
}

/// Panels the board has.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_display_count() -> u8 {
    1
}

/// The pad grid, sidebar included.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_pad_bounds(cols: *mut u8, rows: *mut u8) {
    // SAFETY: the runner passes valid out-pointers or null.
    unsafe {
        if !cols.is_null() {
            *cols = GRID_COLS;
        }
        if !rows.is_null() {
            *rows = GRID_ROWS;
        }
    }
}

/// The last frame on panel `index`, or null if it was never blitted (or the board lacks it).
#[unsafe(no_mangle)]
pub extern "C" fn host_script_oled_shadow(
    index: u8,
    width: *mut u16,
    height: *mut u16,
) -> *const u8 {
    let s = shadows();
    if index != 0 || s.oled_size.0 == 0 {
        return core::ptr::null();
    }
    // SAFETY: the runner passes valid out-pointers or null.
    unsafe {
        if !width.is_null() {
            *width = s.oled_size.0;
        }
        if !height.is_null() {
            *height = s.oled_size.1;
        }
    }
    s.oled.as_ptr()
}

/// The LED shadow: `LED_COUNT` bytes, 1 = on.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_led_shadow() -> *const u8 {
    shadows().leds.as_ptr()
}

/// The pad shadow: `PAD_COLS * PAD_ROWS` RGB triples, column-major.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_pad_shadow() -> *const u8 {
    shadows().pads.as_ptr()
}

/// Ring `which`'s four levels, or null if the app never set it.
#[unsafe(no_mangle)]
pub extern "C" fn host_script_ring_shadow(which: u8) -> *const u8 {
    let s = shadows();
    match s.ring_written.get(which as usize) {
        Some(true) => s.rings[which as usize].as_ptr(),
        _ => core::ptr::null(),
    }
}
