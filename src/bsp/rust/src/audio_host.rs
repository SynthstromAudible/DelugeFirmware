//! `audio_io.h` — host render pump for the `host_app` feature: a null sink, or with `sim` the desktop simulator's
//! audio device (`deluge_bsp::sim::render_block`), which plays the output and paces the render in real time.
//!
//! On device (`audio.rs`) `deluge_audio_drive` trickles the app's render into
//! the SSI TX DMA ring, paced by the free-running play head. There is no DMA
//! (or any audio device at all) on host: this module instead makes the
//! priority-0 C++ task (`AudioEngine::routine_task`, spawned by
//! `registerTasks()` during `deluge_app_init` — see `scheduler.rs`'s
//! `addRepeatingTask`) actually call `deluge_app_render` each time it drives,
//! discarding the output block (a null sink), so the render graph exercises
//! for real instead of the no-op stub in `host_link_stubs.rs`.
//!
//! A second, preemptive host executor thread (`"deluge-audio"`, spawned in
//! `main.rs`'s `host_app` boot path) is what `scheduler::set_audio_spawner`
//! routes the priority-0 task onto — the host analogue of device `audio.rs`'s
//! `AUDIO_EXEC` SGI executor. `AudioEngine::runRoutine()` (audio_engine.cpp) also
//! calls `routine()` — and so `deluge_audio_drive` — directly and synchronously
//! from `deluge_boot()`, before `registerTasks()` assigns `routine_task_id`
//! ("necessary otherwise Deluge freezes on boot"); that one pre-registration call
//! runs on whichever thread called `deluge_boot` (the host-app executor thread),
//! not `"deluge-audio"` — expected, and `deluge_audio_drive` below accounts for
//! it rather than treating it as a routing failure. Once `registerTasks()`
//! returns, every subsequent call is driven by the scheduled task and runs on
//! `"deluge-audio"`. No ISR and no other task touches this module's state
//! concurrently with a render *from the same thread*, but the render itself can
//! now run concurrently with everything else — the `static mut` buffer access is
//! no longer single-threaded in the way `audio.rs` documents for its own
//! statics; that's this module's whole point.
#![allow(non_upper_case_globals)]

use core::ptr::{addr_of, addr_of_mut};
use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use crate::sys::DelugeStereoSample;

unsafe extern "C" {
    /// The app renders `frames` stereo samples into `output`, reading `frames`
    /// aligned input samples from `input` (app.h). i32-native; no scaling
    /// here. Exact mirror of device `audio.rs`'s import (same signature, same
    /// ABI) — the host-built `deluge_app` object closure exports the same
    /// symbol.
    fn deluge_app_render(
        input: *const DelugeStereoSample,
        output: *mut DelugeStereoSample,
        frames: u32,
    );
    /// A block skipped by [`should_skip_render`]: the app still runs the render's block-start lifetime step (app.h),
    /// so objects handed to the render are let go of and freed on a single-executor harness too.
    fn deluge_app_render_skipped();
}

/// Maximum frames per `deluge_app_render` call. MUST match device `audio.rs`'s
/// `APP_BLOCK_FRAMES` (128, not `host_audio.c`'s 32): the app's internal
/// render-buffer globals (`renderingMemory`/`reverbMemory`) are fixed at this
/// size (board_config.h `SSI_TX_BUFFER_NUM_SAMPLES`), and a larger block
/// makes the app write out of bounds into adjacent globals. This is a real
/// ABI constraint of the linked C++ object, not a host-only pacing choice —
/// so it stays in lock-step with the device value regardless of host pacing.
const APP_BLOCK_FRAMES: usize = 128;

const ZERO: DelugeStereoSample = DelugeStereoSample { l: 0, r: 0 };

// Null-sink render scratch. Plain host statics (no SDRAM section on host —
// that's a device linker-script concept); the app renders into RENDER_BLOCK
// reads INPUT_BLOCK: silence, unless a scenario arms the test input signal
// (`deluge_host_test_input`). The render output is discarded unless captured.
static mut RENDER_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];
static mut INPUT_BLOCK: [DelugeStereoSample; APP_BLOCK_FRAMES] = [ZERO; APP_BLOCK_FRAMES];

/// Monotonic host frame cursor: total frames rendered so far, advanced by
/// `APP_BLOCK_FRAMES` on every `deluge_audio_drive` call. Stands in for the
/// device's DMA play head (`tx_play_frame()` in `audio.rs`) — there is no
/// real playback device on host, so "now" is just "how much has been
/// rendered", which is exactly what `deluge_audio_frames_until_block_offset`
/// needs for the (non-sample-accurate) MIDI-gate timing queries the null-sink
/// harness exercises.
static CURSOR: AtomicU64 = AtomicU64::new(0);
/// Cursor value at the start of the most recent render block (the host
/// stand-in for `audio.rs`'s `BLOCK_START_WRITE`/`BLOCK_START_PLAY` pair — on
/// host both collapse to the same value since there's no separate write vs.
/// play head).
static BLOCK_START: AtomicU64 = AtomicU64::new(0);

/// Count of `deluge_audio_drive` calls, for the periodic render-progress log.
static DRIVE_COUNT: AtomicU64 = AtomicU64::new(0);
/// Log a render-progress line every this many drives (plus always on the
/// first), so `deluge_app_render` actually firing is observable without
/// flooding the log at ~44100/128 Hz.
const LOG_EVERY: u64 = 500;

/// Set once a render is observed running on the `"deluge-audio"` executor
/// thread, i.e. the priority-0 task genuinely routed through
/// `scheduler::set_audio_spawner` rather than staying on whichever thread
/// happened to call `deluge_app_render` (see this module's doc comment re:
/// the pre-registration synchronous call from `deluge_boot`). Polled by the
/// `host_app` boot smoke in `main.rs` before it lets the process exit.
static AUDIO_THREAD_RENDER_SEEN: AtomicBool = AtomicBool::new(false);

/// `true` once a `deluge_audio_drive` call has been observed on the
/// `"deluge-audio"` thread — the host-side evidence that the priority-0 task
/// routed to the second (audio) executor.
pub fn audio_thread_render_seen() -> bool {
    AUDIO_THREAD_RENDER_SEEN.load(Ordering::Relaxed)
}

/// Render-mode gate: mirrors `host_audio.c`'s own `DELUGE_RENDER` env-var check
/// (cached once, same shape). `host_app` never sets this variable,
/// so this reads `false` and changes nothing for them.
///
/// Purely a diagnostic trim: when set, skips the per-call bookkeeping below (a
/// thread-name `String` allocation plus `log::info!` calls) that would
/// otherwise run on every single rendered block of what can be a multi-minute
/// offline export. `deluge_app_render` itself is unaffected by this flag — see
/// [`should_skip_render`] for the guard that actually matters.
fn render_mode() -> bool {
    static RENDER_MODE: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *RENDER_MODE.get_or_init(|| {
        std::env::var("DELUGE_RENDER")
            .map(|v| !v.is_empty() && v != "0")
            .unwrap_or(false)
    })
}

/// Whether THIS `deluge_audio_drive` call must skip `deluge_app_render`
/// entirely — the real null-sink guard (`render_mode` above is a separate,
/// cosmetic trim).
///
/// `deluge_audio_drive` is reached from two fundamentally different callers
/// that share this one function, and only one of them is safe to render from
/// on a single-executor host harness (`golden_vt_render`, which
/// — per this repo's "deterministic single-threaded executor ONLY" constraint —
/// never call [`crate::scheduler::set_audio_spawner`], so the priority-0 audio
/// task falls back to running on the SAME executor as everything else, not a
/// separate thread):
///
/// - **Before boot has finished** (`crate::boot::finished()` false) —
///   `AudioEngine::runRoutine()`'s one-time pre-registration call from
///   `deluge_boot()` (see this module's doc comment: skipping it freezes
///   boot). Never skipped.
/// - **After boot, on host_app's real `"deluge-audio"` OS thread**
///   (`crate::scheduler::set_audio_spawner` called) — the NORMAL, load-bearing
///   render path. Never skipped.
/// - **After boot, anywhere else** — the priority-0 `AudioEngine::routine_task`'s
///   own periodic tick on a single-executor harness. This call can reach C++
///   sample-playback code that does a SYNCHRONOUS efatfs read; under
///   `sim_latency` that read needs a `Timer` fired by a separate task
///   (`sim_latency::pump`) to ever resolve, and a non-yielding `block_on` can
///   never let that task get polled: a full-process livelock (a real offline
///   `StemExport` run against a sample-backed song hung at 100% CPU inside
///   exactly this call chain). Skipping it is safe: the render this call would
///   have produced is discarded anyway (a null sink).
fn should_skip_render() -> bool {
    if !crate::boot::finished() || render_after_boot() || REALTIME_RENDER.load(Ordering::Relaxed) {
        return false;
    }
    // After boot: skip UNLESS this is host_app's real separate audio OS thread,
    // where this is the legitimate, load-bearing render path.
    let thread_name = std::thread::current().name().unwrap_or("").to_string();
    thread_name != "deluge-audio"
}

/// Set by a scenario that plays a song in real time (`render_song`): render every block after boot, as the device
/// does. Every other scenario leaves it clear, and keeps the skip its recorded output depends on.
static REALTIME_RENDER: AtomicBool = AtomicBool::new(false);

/// Render every block after boot for the rest of this run; see [`REALTIME_RENDER`].
pub fn set_realtime_render(on: bool) {
    REALTIME_RENDER.store(on, Ordering::Relaxed);
}

/// The captured output, while capture is on: every rendered block's stereo samples, as little-endian `i32` pairs.
static CAPTURE: std::sync::Mutex<Option<std::io::BufWriter<std::fs::File>>> =
    std::sync::Mutex::new(None);
static CAPTURE_FRAMES: AtomicU64 = AtomicU64::new(0);

/// Start capturing the rendered output into the file `RENDER_PCM_OUT` names. Without it, only the frame count is
/// kept. C ABI for the scenario that starts playback.
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

/// Stop capturing and flush what was captured. Returns the frames captured.
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
        // SAFETY: single-threaded (see module doc): the render that wrote RENDER_BLOCK has returned, and nothing else
        // touches it until the next drive.
        let block = unsafe { &*addr_of!(RENDER_BLOCK) };
        for s in block {
            file.write_all(&s.l.to_le_bytes())
                .expect("RENDER_PCM_OUT: writing the capture");
            file.write_all(&s.r.to_le_bytes())
                .expect("RENDER_PCM_OUT: writing the capture");
        }
    }
}

/// Blocks rendered since boot; see [`drive_count`]. C ABI for scenarios.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_render_count() -> u64 {
    drive_count()
}

/// `DELUGE_RENDER_AFTER_BOOT=1`: never skip, so a single-executor harness renders every block after boot as
/// well. Any synchronous card call a render makes is then counted as an audio-tier call (`tier_meter`), which is
/// how the calls the skip exists to avoid are found.
pub fn render_after_boot() -> bool {
    static ON: std::sync::OnceLock<bool> = std::sync::OnceLock::new();
    *ON.get_or_init(|| std::env::var_os("DELUGE_RENDER_AFTER_BOOT").is_some_and(|v| v == "1"))
}

/// Frame the test input signal counts from, while it is armed; `u64::MAX` while it is not.
static TEST_INPUT_ORIGIN: AtomicU64 = AtomicU64::new(u64::MAX);

/// Arm (or disarm) the test input signal, with frame 0 at the current render position. C ABI for the scenario.
///
/// The signal feeds the INPUT_L/R/STEREO sources something to render: a sawtooth 128 frames long on the left and
/// one 256 frames long on the right, both at full q31 scale and a pure function of the frame, so every run and
/// architecture feeds the same samples. The channels differ so a render can tell them apart.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_test_input(on: bool) {
    let origin = if on {
        CURSOR.load(Ordering::Relaxed)
    } else {
        u64::MAX
    };
    TEST_INPUT_ORIGIN.store(origin, Ordering::Relaxed);
}

/// Fill INPUT_BLOCK for the block starting at `block_start`: the test signal while armed, else `live` (the
/// simulator's captured input, as `[-1, 1]` frames), else silence.
fn fill_input_block(block_start: u64, live: Option<&[[f32; 2]]>) {
    let origin = TEST_INPUT_ORIGIN.load(Ordering::Relaxed);
    // SAFETY: the render that reads INPUT_BLOCK has not started, and nothing else writes it (see the module doc).
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

/// A DIN MIDI note script: each event's bytes reach the app once the render has passed the event's frame, counted
/// from the frame the script was armed at. What the synth golden masters play (`render_preset`).
struct DinScript {
    /// `(frame, bytes)`, in nondecreasing frame order.
    events: Vec<(u64, Vec<u8>)>,
    origin: u64,
    next: usize,
    byte: usize,
}

static DIN_SCRIPT: std::sync::Mutex<Option<DinScript>> = std::sync::Mutex::new(None);

/// Parse a note script: one event per line, `<frame> <hex byte>...`, with `#` starting a comment. Frames must not
/// decrease.
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

/// Arm the note script at `path`, with frame 0 at the current render position. Returns the number of events, or -1
/// when the script cannot be read or parsed (the reason is logged). C ABI for the scenario.
///
/// # Safety
/// `path` is a NUL-terminated string.
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

/// Events the app has read the whole of.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_host_midi_script_released() -> u32 {
    DIN_SCRIPT
        .lock()
        .unwrap()
        .as_ref()
        .map_or(0, |script| script.next as u32)
}

unsafe extern "C" {
    /// The synth's random stream (util/waves.h): oscillator phases, noise and random modulation all draw from it.
    static mut jcong: u32;
}

/// `jcong`'s value at boot (util/waves.cpp).
const JCONG_BOOT_SEED: u32 = 380_116_160;

/// The seed the script's first byte restarts the random stream from: `RENDER_RANDOM_SEED`, or the boot seed.
/// Another seed measures how much a render depends on its random draws, as a re-baseline needs to know.
fn script_random_seed() -> u32 {
    static SEED: std::sync::OnceLock<u32> = std::sync::OnceLock::new();
    *SEED.get_or_init(|| {
        std::env::var("RENDER_RANDOM_SEED")
            .ok()
            .and_then(|s| s.parse().ok())
            .unwrap_or(JCONG_BOOT_SEED)
    })
}

/// The next scripted DIN byte whose frame the render has passed, if any. What the host's
/// `deluge_midi_din_read_timed` returns.
///
/// Handing over the script's first byte restarts the synth's random stream from its boot seed (or
/// `RENDER_RANDOM_SEED`), so what a note's randomness sounds like depends on the script alone, not on how many
/// random numbers boot and loading drew first.
pub fn din_script_read() -> Option<u8> {
    let mut guard = DIN_SCRIPT.lock().unwrap();
    let script = guard.as_mut()?;
    let (frame, bytes) = script.events.get(script.next)?;
    if CURSOR.load(Ordering::Relaxed) - script.origin < *frame {
        return None;
    }
    if script.next == 0 && script.byte == 0 {
        // SAFETY: a plain store to an app global; the app reads it only on the tiers that run MIDI and the render,
        // and this runs on the MIDI tier, before the byte that starts the first note is handed over.
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

/// Total `deluge_audio_drive` calls observed so far (monotonic). Polled by
/// `main.rs`'s `host_app` boot path to bound the post-boot concurrency soak by
/// render-cycle count as well as wall-clock time.
pub fn drive_count() -> u64 {
    DRIVE_COUNT.load(Ordering::Relaxed)
}

/// Two cases, as [`should_skip_render`] draws them:
///
/// - **host_app** (`scheduler::set_audio_spawner` called): the render runs on its own `"deluge-audio"` thread, the
///   device's preemptive audio executor's stand-in. Rendering here would run a second render alongside it, so sleep
///   briefly and let that thread render.
/// - **A single-executor harness** (golden_vt_render): the audio task is a cooperative task on the caller's own
///   executor, and cannot run while the caller waits. Run its step inline: [`deluge_audio_drive`] enters the audio
///   tier itself, and after boot it runs the block-start step of the render it skips
///   (`deluge_app_render_skipped`), which is what lets go of what was handed over.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_await_block() {
    if crate::scheduler::has_audio_executor() {
        std::thread::sleep(std::time::Duration::from_millis(1));
    } else {
        let _ = deluge_audio_drive();
    }
}

/// Render via the real app render path: into the simulator's audio device while it has room (`sim`), else one block
/// into the null sink. Called directly by the priority-0 C++ task (`AudioEngine::routine_task`)
/// — see this module's doc comment; there is no separate pump task here.
///
/// Per the `audio_io.h` contract, the return value is the number of times
/// `deluge_app_render` was invoked this call (0 = no new audio needed), NOT a
/// frame count — mirrors device `audio.rs`. The null sink renders exactly one block per call and
/// returns 1, UNLESS [`should_skip_render`] says this specific call must skip
/// `deluge_app_render` entirely, in which case it returns 0 (a normal,
/// contract-valid "no new audio needed" outcome).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_drive() -> u32 {
    let _tier = crate::tier::enter(crate::tier::Tier::Audio);
    if should_skip_render() {
        // No audio, but the render's lifetime step still runs on this (render-tier) call: without it the epoch
        // would stand still and nothing retired would ever be let go of or freed.
        // SAFETY: an app entry point with no arguments, called on the render tier as app.h requires.
        unsafe { deluge_app_render_skipped() };
        return 0;
    }

    #[cfg(feature = "sim")]
    if deluge_bsp::sim::panel().is_some() {
        return drive_simulator();
    }

    render_block(None);
    1
}

/// The simulator's audio device is the clock, as the DMA play head is on the device: render while its output has
/// room, and at most two blocks a call, as the device's drive does.
#[cfg(feature = "sim")]
fn drive_simulator() -> u32 {
    let mut renders = 0;
    while renders < 2
        && deluge_bsp::sim::render_block(|frames| {
            render_block(Some(frames));
            // SAFETY: single-threaded (see module doc): the render that wrote RENDER_BLOCK has returned.
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

/// Render one block from the cursor into RENDER_BLOCK, reading `live` as its input (see [`fill_input_block`]).
fn render_block(live: Option<&[[f32; 2]]>) {
    let block_start = CURSOR.load(Ordering::Relaxed);
    BLOCK_START.store(block_start, Ordering::Relaxed);
    fill_input_block(block_start, live);

    // SAFETY: single-threaded (see module doc) — no other task or ISR touches
    // RENDER_BLOCK/INPUT_BLOCK. Pointers cover exactly APP_BLOCK_FRAMES
    // elements of the app's native stereo-sample type, matching the device
    // `audio.rs` call site's ABI exactly.
    unsafe {
        deluge_app_render(
            addr_of!(INPUT_BLOCK) as *const DelugeStereoSample,
            addr_of_mut!(RENDER_BLOCK) as *mut DelugeStereoSample,
            APP_BLOCK_FRAMES as u32,
        );
    }
    // Discarded unless captured or the simulator plays it: there is no DAC on the host.

    CURSOR.store(block_start + APP_BLOCK_FRAMES as u64, Ordering::Relaxed);
    capture_block();

    let n = DRIVE_COUNT.fetch_add(1, Ordering::Relaxed) + 1;
    if render_mode() {
        // See render_mode()'s doc: skip the diagnostic bookkeeping only — the
        // render above already happened unconditionally.
        return;
    }
    let thread_name = std::thread::current()
        .name()
        .unwrap_or("<unnamed>")
        .to_string();
    if n == 1 {
        // Expected to read the host-app executor thread, NOT "deluge-audio" —
        // this is `AudioEngine::runRoutine()`'s pre-registration direct call
        // (routine_task_id is still -1), made before `registerTasks()` has had
        // a chance to route anything anywhere. See this module's doc comment.
        log::info!("audio: first (pre-registration) render on thread {thread_name:?}");
    }
    if thread_name == "deluge-audio" && !AUDIO_THREAD_RENDER_SEEN.swap(true, Ordering::Relaxed) {
        // Evidence that the priority-0 task's *scheduled* render (not the
        // pre-registration one above) genuinely routed to the second executor
        // thread via `scheduler::set_audio_spawner`, and is running there
        // concurrently with the main/host-app executor thread.
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
    // Host stand-in for device `audio.rs`'s TX-ring play-head math: "now" is
    // the monotonic render cursor (no real DMA play head to poll), and there
    // is no ring to mask against, so this is plain (non-panicking) wrapping
    // arithmetic rather than a masked ring offset. The null-sink harness
    // doesn't need sample-accurate MIDI-gate timing, only a sane,
    // non-panicking answer.
    let play_now = CURSOR.load(Ordering::Relaxed);
    let block_start = BLOCK_START.load(Ordering::Relaxed);

    if !elapsed_frames_out.is_null() {
        // SAFETY: caller-provided out param, matching device `audio.rs`'s
        // contract (non-null implies writable).
        unsafe {
            *elapsed_frames_out = play_now.wrapping_sub(block_start) as u32;
        }
    }

    block_start
        .wrapping_add(offset_frames as u64)
        .wrapping_sub(play_now) as u32
}
