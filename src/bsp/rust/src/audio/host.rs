//! `libdeluge/audio_io.h` on host: renders each block for real, into a null sink or (with `sim`)
//! the emulator's audio device, which plays it and paces the render in real time.
//!
//! The render task ([`crate::audio::render::render_task`]) ticks the app, whose
//! `AudioEngine::routine_task` calls [`deluge_audio_drive`], which calls `deluge_app_render` and
//! discards the block unless it is captured ([`deluge_host_capture_start`]). The host runtime
//! spawns that task on a separate `"deluge-audio"` executor thread ([`set_audio_executor`]), the
//! stand-in for the device's preemptive audio executor; single-executor harnesses such as
//! `golden_vt_render` spawn it on their one executor instead (see `should_skip_render`).
//!
//! `AudioEngine::runRoutine()` also calls the drive during boot, before the render task's first
//! tick, on the boot thread rather than `"deluge-audio"`.
//!
//! The render buffers are `static mut`: only the render touches them, one block at a time.
//!
//! The host test hooks here (`deluge_host_*`) are C ABI for the scenarios: output capture, a
//! deterministic test input signal, and a scripted DIN MIDI stream.
#![allow(non_upper_case_globals)]

use core::ptr::{addr_of, addr_of_mut};
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use crate::sys::DelugeStereoSample;

/// Frames per `deluge_app_render` call; must equal the device drive's.
///
/// The app's render buffers are fixed at `SSI_TX_BUFFER_NUM_SAMPLES` (128, `board_config.h`); a
/// larger block writes out of bounds into adjacent globals.
const APP_BLOCK_FRAMES: usize = 128;

const ZERO: DelugeStereoSample = DelugeStereoSample { l: 0, r: 0 };

// Render scratch: the app renders into RENDER_BLOCK and reads INPUT_BLOCK (see fill_input_block).
static mut RENDER_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];
static mut INPUT_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];

/// Total frames rendered: the host's stand-in for the device's DMA play head. "Now" is how much
/// has been rendered.
static CURSOR: AtomicU64 = AtomicU64::new(0);
/// [`CURSOR`] at the start of the most recent render block. With no separate write and play
/// heads, it stands in for both of the device's block anchors.
static BLOCK_START: AtomicU64 = AtomicU64::new(0);

/// Blocks rendered, for [`drive_count`] and the progress log.
static DRIVE_COUNT: AtomicU64 = AtomicU64::new(0);
/// Log a render-progress line every this many blocks (and on the first), so rendering is visible
/// without flooding the log at ~344 blocks a second.
const LOG_EVERY: u64 = 500;

/// Set once a render runs on the `"deluge-audio"` thread, i.e. the render task was spawned on the
/// audio executor.
static AUDIO_THREAD_RENDER_SEEN: AtomicBool = AtomicBool::new(false);

/// Whether the render has an executor of its own (the threaded host app's `"deluge-audio"`
/// thread) rather than sharing a single-executor harness's one.
static AUDIO_EXECUTOR: AtomicBool = AtomicBool::new(false);

/// Marks the render as having an executor of its own. Call from that executor's thread, before
/// `deluge_app_init`.
pub fn set_audio_executor() {
    AUDIO_EXECUTOR.store(true, Ordering::Relaxed);
}

/// Returns whether a render has run on the `"deluge-audio"` thread; the host runtime's boot check
/// requires it.
pub fn audio_thread_render_seen() -> bool {
    AUDIO_THREAD_RENDER_SEEN.load(Ordering::Relaxed)
}

/// Whether `DELUGE_RENDER` is set (non-empty, not "0"); read once. `golden_vt_render` sets it.
///
/// A diagnostic trim only: it skips the per-block thread-name and log bookkeeping in
/// [`render_block`]. Whether a block renders is [`should_skip_render`]'s decision.
fn render_mode() -> bool {
    static RENDER_MODE: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *RENDER_MODE.get_or_init(|| {
        std::env::var("DELUGE_RENDER")
            .map(|v| !v.is_empty() && v != "0")
            .unwrap_or(false)
    })
}

/// Whether this [`deluge_audio_drive`] call must skip `deluge_app_render`.
///
/// Never skipped:
/// - before boot finishes (`AudioEngine::runRoutine()`'s call from `deluge_boot()`; skipping it
///   freezes boot);
/// - on the `"deluge-audio"` thread, the normal render path;
/// - when [`render_after_boot`] or [`REALTIME_RENDER`] asks for every block.
///
/// Otherwise — after boot on a single-executor harness, where the render task runs on the same
/// executor as everything else — the call is skipped: nothing consumes that block's output.
fn should_skip_render() -> bool {
    if !crate::boot::finished() || render_after_boot() || REALTIME_RENDER.load(Ordering::Relaxed) {
        return false;
    }
    // After boot, render only on the separate audio thread.
    let thread_name = std::thread::current().name().unwrap_or("").to_string();
    thread_name != "deluge-audio"
}

/// Set by a scenario that plays a song in real time (`render_song`): render every block after
/// boot, as the device does. Other scenarios leave it clear; their recorded output depends on the
/// skip.
static REALTIME_RENDER: AtomicBool = AtomicBool::new(false);

/// Sets `REALTIME_RENDER` for the rest of this run.
pub fn set_realtime_render(on: bool) {
    REALTIME_RENDER.store(on, Ordering::Relaxed);
}

/// The capture file, while capture is on: every rendered block's stereo samples, as little-endian
/// `i32` pairs.
static CAPTURE: std::sync::Mutex<Option<std::io::BufWriter<std::fs::File>>> =
    std::sync::Mutex::new(None);
static CAPTURE_FRAMES: AtomicU64 = AtomicU64::new(0);

/// Starts capturing the rendered output into the file `RENDER_PCM_OUT` names; without that
/// variable only the frame count is kept.
///
/// # Panics
///
/// Panics if the file cannot be created.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_capture_start() {
    let file = std::env::var_os("RENDER_PCM_OUT").map(|path| {
        std::io::BufWriter::new(std::fs::File::create(&path).unwrap_or_else(|e| {
            panic!(
                "RENDER_PCM_OUT {}: {e}",
                std::path::Path::new(&path).display()
            )
        }))
    });
    *CAPTURE.lock().unwrap() = file;
    CAPTURE_FRAMES.store(0, Ordering::Relaxed);
    CAPTURING.store(true, Ordering::Relaxed);
}

/// Stops capturing, flushes the capture file, and returns the number of frames captured.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_capture_stop() -> u64 {
    CAPTURING.store(false, Ordering::Relaxed);
    if let Some(mut file) = CAPTURE.lock().unwrap().take() {
        use std::io::Write as _;
        file.flush().expect("RENDER_PCM_OUT: flushing the capture");
    }
    CAPTURE_FRAMES.load(Ordering::Relaxed)
}

static CAPTURING: AtomicBool = AtomicBool::new(false);

fn capture_block() {
    if !CAPTURING.load(Ordering::Relaxed) {
        return;
    }
    CAPTURE_FRAMES.fetch_add(APP_BLOCK_FRAMES as u64, Ordering::Relaxed);
    if let Some(file) = CAPTURE.lock().unwrap().as_mut() {
        use std::io::Write as _;
        // SAFETY: the render that wrote RENDER_BLOCK has returned, and nothing else touches it
        // until the next drive.
        let block = unsafe { &*addr_of!(RENDER_BLOCK) };
        for s in block {
            file.write_all(&s.l.to_le_bytes())
                .expect("RENDER_PCM_OUT: writing the capture");
            file.write_all(&s.r.to_le_bytes())
                .expect("RENDER_PCM_OUT: writing the capture");
        }
    }
}

/// Returns the blocks rendered since boot ([`drive_count`]).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_render_count() -> u64 {
    drive_count()
}

/// Whether `DELUGE_RENDER_AFTER_BOOT=1`: never skip, so a single-executor harness renders every
/// block after boot too. Any synchronous card call a render makes then counts as an audio-tier call
/// (`tier_meter`), which is how the calls the skip avoids are found.
pub fn render_after_boot() -> bool {
    static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *ON.get_or_init(|| std::env::var_os("DELUGE_RENDER_AFTER_BOOT").is_some_and(|v| v == "1"))
}

/// The frame the test input signal counts from while armed; `u64::MAX` while not.
static TEST_INPUT_ORIGIN: AtomicU64 = AtomicU64::new(u64::MAX);

/// Arms (or disarms) the test input signal, with frame 0 at the current render position.
///
/// The signal gives the INPUT_L/R/STEREO sources something to render: a full-scale q31 sawtooth,
/// 128 frames long on the left and 256 on the right (so a render can tell them apart). It is a pure
/// function of the frame, so every run and architecture feeds the same samples.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_test_input(on: bool) {
    let origin = if on {
        CURSOR.load(Ordering::Relaxed)
    } else {
        u64::MAX
    };
    TEST_INPUT_ORIGIN.store(origin, Ordering::Relaxed);
}

/// Whether the headphone socket reads as occupied ([`deluge_host_headphones`]).
static HEADPHONES: AtomicBool = AtomicBool::new(false);

/// Plugs (or unplugs) headphones: the headphone-detect signal reads `on` from now on.
///
/// The app renders in stereo while headphones are plugged in (`AudioEngine::renderInStereo`, set by
/// its input routine), and in mono, as through the device's speaker, while they are not. Unplugged
/// at boot, so a harness renders mono unless it asks for stereo.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_headphones(on: bool) {
    HEADPHONES.store(on, Ordering::Relaxed);
}

/// What the headphone-detect signal reads.
pub fn headphones() -> bool {
    HEADPHONES.load(Ordering::Relaxed)
}

/// Fills `INPUT_BLOCK` for the block starting at `block_start`: the test signal while armed, else
/// `live` (the emulator's captured input, as `[-1, 1]` frames), else silence.
fn fill_input_block(block_start: u64, live: Option<&[[f32; 2]]>) {
    let origin = TEST_INPUT_ORIGIN.load(Ordering::Relaxed);
    // SAFETY: the render that reads INPUT_BLOCK has not started, and nothing else writes it.
    let block = unsafe { &mut *addr_of_mut!(INPUT_BLOCK) };
    for (i, s) in block.iter_mut().enumerate() {
        *s = if origin == u64::MAX {
            live.and_then(|frames| frames.get(i))
                .map_or(ZERO, |f| DelugeStereoSample {
                    l: f32_to_q31(f[0]),
                    r: f32_to_q31(f[1]),
                })
        } else {
            let n = (block_start - origin + i as u64) as u32;
            DelugeStereoSample {
                l: n.wrapping_mul(0x0200_0000) as i32,
                r: n.wrapping_mul(0x0100_0000) as i32,
            }
        };
    }
}

/// A DIN MIDI note script, as the synth golden masters play (`render_preset`): each event's bytes
/// reach the app once the render passes the event's frame, counted from where the script was armed.
struct DinScript {
    /// `(frame, bytes)`, in nondecreasing frame order.
    events: Vec<(u64, Vec<u8>)>,
    origin: u64,
    next: usize,
    byte: usize,
}

static DIN_SCRIPT: std::sync::Mutex<Option<DinScript>> = std::sync::Mutex::new(None);

/// Parses a note script: one event per line, `<frame> <hex byte>...`, `#` starting a comment.
///
/// # Errors
///
/// Returns a message naming the line for a bad frame or byte, an event with no bytes, or a frame
/// that decreases.
fn parse_din_script(text: &str) -> Result<Vec<(u64, Vec<u8>)>, String> {
    let mut events: Vec<(u64, Vec<u8>)> = Vec::new();
    for (n, line) in text.lines().enumerate() {
        let line = line.split('#').next().unwrap_or("");
        let mut tokens = line.split_whitespace();
        let Some(frame) = tokens.next() else {
            continue;
        };
        let frame: u64 = frame
            .parse()
            .map_err(|_| format!("line {}: frame {frame:?} is not a number", n + 1))?;
        let bytes = tokens
            .map(|t| {
                u8::from_str_radix(t, 16)
                    .map_err(|_| format!("line {}: {t:?} is not a hex byte", n + 1))
            })
            .collect::<Result<Vec<u8>, String>>()?;
        if bytes.is_empty() {
            return Err(format!("line {}: an event with no bytes", n + 1));
        }
        if events.last().is_some_and(|(last, _)| frame < *last) {
            return Err(format!(
                "line {}: frame {frame} is before the event above it",
                n + 1
            ));
        }
        events.push((frame, bytes));
    }
    Ok(events)
}

/// Arms the note script at `path`, with frame 0 at the current render position.
///
/// Returns the number of events, or -1 (with the reason logged) if the script cannot be read or
/// parsed.
///
/// # Safety
///
/// `path` must be a valid NUL-terminated string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_host_midi_script_start(path: *const core::ffi::c_char) -> i32 {
    // SAFETY: the caller passes a NUL-terminated path.
    let path = unsafe { core::ffi::CStr::from_ptr(path) }
        .to_string_lossy()
        .into_owned();
    let events = match std::fs::read_to_string(&path)
        .map_err(|e| e.to_string())
        .and_then(|text| parse_din_script(&text))
    {
        Ok(events) => events,
        Err(e) => {
            log::error!("note script {path}: {e}");
            return -1;
        }
    };
    let count = events.len() as i32;
    *DIN_SCRIPT.lock().unwrap() = Some(DinScript {
        events,
        origin: CURSOR.load(Ordering::Relaxed),
        next: 0,
        byte: 0,
    });
    count
}

/// Returns the number of script events the app has read in full.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_midi_script_released() -> u32 {
    DIN_SCRIPT
        .lock()
        .unwrap()
        .as_ref()
        .map_or(0, |script| script.next as u32)
}

unsafe extern "C" {
    /// The synth's random stream (`util/waves.h`): oscillator phases, noise and random modulation
    /// all draw from it.
    static mut jcong: u32;
}

/// `jcong`'s value at boot (`util/waves.cpp`).
const JCONG_BOOT_SEED: u32 = 380_116_160;

/// Returns the seed the script's first byte restarts the random stream from: `RENDER_RANDOM_SEED`,
/// or the boot seed. Another seed shows how much a render depends on its random draws.
fn script_random_seed() -> u32 {
    static SEED: std::sync::OnceLock<u32> = std::sync::OnceLock::new();
    *SEED.get_or_init(|| {
        std::env::var("RENDER_RANDOM_SEED")
            .ok()
            .and_then(|s| s.parse().ok())
            .unwrap_or(JCONG_BOOT_SEED)
    })
}

/// Returns the next scripted DIN byte whose frame the render has passed, if any; the host's
/// `deluge_midi_din_read_timed` serves it.
///
/// Handing over the first byte restarts the synth's random stream from `script_random_seed`, so a
/// note's randomness depends on the script alone, not on how many random numbers boot and loading
/// drew.
pub fn din_script_read() -> Option<u8> {
    let mut guard = DIN_SCRIPT.lock().unwrap();
    let script = guard.as_mut()?;
    let (frame, bytes) = script.events.get(script.next)?;
    if CURSOR.load(Ordering::Relaxed) - script.origin < *frame {
        return None;
    }
    if script.next == 0 && script.byte == 0 {
        // SAFETY: a plain store to an app global, on the MIDI tier, before the byte that starts the
        // first note is handed over.
        unsafe { jcong = script_random_seed() };
    }
    let b = bytes[script.byte];
    script.byte += 1;
    if script.byte == bytes.len() {
        script.byte = 0;
        script.next += 1;
    }
    Some(b)
}

/// Returns the number of blocks rendered so far (monotonic).
pub fn drive_count() -> u64 {
    DRIVE_COUNT.load(Ordering::Relaxed)
}

/// Waits for the render to advance a block.
///
/// - With an audio executor (the `"deluge-audio"` thread): sleeps briefly and lets that thread
///   render; rendering here would run a second render alongside it.
/// - On a single-executor harness: the render task cannot run while the caller waits, so this runs
///   [`deluge_audio_drive`] inline. After boot that skips the render but still runs its block-start
///   step (`deluge_app_render_skipped`), which releases what was handed over.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_await_block() {
    if AUDIO_EXECUTOR.load(Ordering::Relaxed) {
        std::thread::sleep(std::time::Duration::from_millis(1));
    } else {
        let _ = deluge_audio_drive();
    }
}

/// Renders on the audio tier: into the emulator's audio device while it has room (`sim`), else one
/// block into the null sink.
///
/// Returns the number of `deluge_app_render` calls made (not frames), per `libdeluge/audio_io.h`:
/// 1 for the null sink, 0 when `should_skip_render` skips.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_drive() -> u32 {
    let _tier = crate::tier::enter(crate::tier::Tier::Audio);
    if should_skip_render() {
        // The render's lifetime step still runs: without it the epoch stands still and nothing
        // retired is ever released or freed. SAFETY: an app entry point with no arguments, called
        // on the render tier as app.h requires.
        unsafe { crate::sys::deluge_app_render_skipped() };
        return 0;
    }

    #[cfg(feature = "sim")]
    if deluge_bsp::sim::panel().is_some() {
        return drive_simulator();
    }

    render_block(None);
    1
}

/// Renders into the emulator's audio device, which is the clock as the DMA play head is on the
/// device: while its output has room, at most two blocks a call, as the device drive does.
#[cfg(feature = "sim")]
fn drive_simulator() -> u32 {
    let mut renders = 0;
    while renders < 2
        && deluge_bsp::sim::render_block(|frames| {
            render_block(Some(frames));
            // SAFETY: the render that wrote RENDER_BLOCK has returned.
            let out = unsafe { &*addr_of!(RENDER_BLOCK) };
            for (frame, s) in frames.iter_mut().zip(out) {
                *frame = [q31_to_f32(s.l), q31_to_f32(s.r)];
            }
        })
    {
        renders += 1;
    }
    renders
}

#[cfg(feature = "sim")]
fn q31_to_f32(s: i32) -> f32 {
    s as f32 / 2_147_483_648.0
}

fn f32_to_q31(s: f32) -> i32 {
    // `as` saturates, so full scale lands on i32::MAX rather than wrapping.
    (s * 2_147_483_648.0) as i32
}

/// Renders one block at the cursor into `RENDER_BLOCK`, with `live` as input (see
/// [`fill_input_block`]).
fn render_block(live: Option<&[[f32; 2]]>) {
    let block_start = CURSOR.load(Ordering::Relaxed);
    BLOCK_START.store(block_start, Ordering::Relaxed);
    fill_input_block(block_start, live);

    // SAFETY: nothing else touches RENDER_BLOCK/INPUT_BLOCK during the render; both hold exactly
    // APP_BLOCK_FRAMES frames.
    unsafe {
        crate::sys::deluge_app_render(
            addr_of!(INPUT_BLOCK) as *const DelugeStereoSample,
            addr_of_mut!(RENDER_BLOCK) as *mut DelugeStereoSample,
            APP_BLOCK_FRAMES as u32,
        );
    }
    // Discarded unless captured or the emulator plays it.

    CURSOR.store(block_start + APP_BLOCK_FRAMES as u64, Ordering::Relaxed);
    capture_block();

    let n = DRIVE_COUNT.fetch_add(1, Ordering::Relaxed) + 1;
    if render_mode() {
        // Skip only the diagnostic bookkeeping.
        return;
    }
    let thread_name = std::thread::current()
        .name()
        .unwrap_or("<unnamed>")
        .to_string();
    if n == 1 {
        // The pre-registration call from deluge_boot(), on the boot thread.
        log::info!("audio: first (pre-registration) render on thread {thread_name:?}");
    }
    if thread_name == "deluge-audio" && !AUDIO_THREAD_RENDER_SEEN.swap(true, Ordering::Relaxed) {
        // The scheduled render reached the separate audio executor thread.
        log::info!("audio: render #{n} routed onto the \"deluge-audio\" executor thread");
    }
    if n == 1 || n.is_multiple_of(LOG_EVERY) {
        log::info!("audio: rendered {n} blocks (last on thread {thread_name:?})");
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_frames_until_block_offset(
    offset_frames: u32,
    elapsed_frames_out: *mut u32,
) -> u32 {
    // "Now" is the render cursor and there is no ring to mask against: plain wrapping arithmetic.
    // Host timing need only be sane, not sample-accurate.
    let play_now = CURSOR.load(Ordering::Relaxed);
    let block_start = BLOCK_START.load(Ordering::Relaxed);

    if !elapsed_frames_out.is_null() {
        // SAFETY: a non-null out param is writable, per the ABI contract.
        unsafe {
            *elapsed_frames_out = play_now.wrapping_sub(block_start) as u32;
        }
    }

    block_start
        .wrapping_add(offset_frames as u64)
        .wrapping_sub(play_now) as u32
}

abi_pin!(
    deluge_audio_await_block,
    deluge_audio_drive,
    deluge_audio_frames_until_block_offset
);
