//! The Rust async audio-file load task: drives a card sample's file open, size
//! resolution and header parse on this executor, between the two I/O-free
//! halves the C++ side exposes over `include/libdeluge/audio_load.h`.
//!
//! `AudioFileManager::buildAudioFileFromCard`'s SAMPLE path is a prologue
//! (allocate + adopt a `Sample`), some I/O (resolve the alternate load dir, pick
//! and open a candidate path, size it, open the Sample's own read stream, walk
//! the header), and an epilogue (apply the parsed geometry, publish). Only the
//! middle is I/O, and only the middle moves here — [`load_once`] is that middle,
//! plus the two calls that bracket it.
//!
//! ## Two compilation tiers
//!
//! Same shape, and for the same reason, as `streaming_loader.rs`: the selector
//! the C++ side calls (`deluge_audio_load_async_active`) must resolve on the
//! Embassy BSP whether or not the async backing is enabled, because a cargo
//! feature cannot reach a C++ `#define`.
//!
//! - Always compiled whenever this module is (the Embassy BSP: device, or host
//!   under `host_app` — see the `mod audio_loader;` cfg in `main.rs`):
//!   [`LOAD_WAKE`], [`deluge_audio_load_async_active`] and
//!   [`deluge_audio_load_enqueue`], which override the `__attribute__((weak))`
//!   C++ fallbacks in `audio_load_abi.cpp`. Every other BSP (legacy/cooperative
//!   sim, rza1) never links this crate and resolves those weak fallbacks
//!   instead, so it always takes the synchronous path.
//! - Feature-gated behind `async_audio_load`: the driver itself ([`LoadOps`],
//!   [`load_once`], `ProdOps`, [`audio_load_task`]).
//!
//! Compiling the driver and *using* it are two different switches.
//! `async_audio_load` (on by default) only builds and spawns the task;
//! `async_audio_load_active` (off by default) is the sole input to
//! [`deluge_audio_load_async_active`], and therefore the only thing that makes
//! the C++ side dispatch a load here at all. With the switch off nothing ever
//! enqueues, so the task waits on [`LOAD_WAKE`] forever and every load takes
//! the synchronous path exactly as before.
//!
//! ## Why the driver is generic over [`LoadOps`]
//!
//! Same constraint as `streaming_loader`: `host_app` (which links the host-built
//! C++ app closure) and this crate's integration tests do not co-compile, so a
//! host test cannot call the real `deluge_audio_load_*` symbols, and defining
//! fakes with those names would collide with the real ones wherever the C++ app
//! *is* linked. The orchestration therefore takes every operation it needs
//! through [`LoadOps`]; `ProdOps` wires that to the real C ABI plus
//! `crate::efatfs_fs` / `crate::efatfs_host_shim`, and
//! `tests/audio_loader_host.rs` wires it to an in-memory double.
//!
//! Note that `apply_geometry` is a [`LoadOps`] method rather than a free
//! function calling the ABI directly: it is a *refusable* call (an invalid
//! `raw_data_format` is rejected outright, see `audio_load.h`), so the driver
//! has to branch on its result, and a host test has to be able to observe it.
//!
//! ## Ownership, and the two resources every path must release
//!
//! `deluge_audio_load_begin` adopts the `Sample` into
//! `AudioFileManager::audioFiles` and gives it a protect-during-setup reason
//! *before* any I/O. From the moment it returns non-NULL, exactly one of
//! `finish` or `abandon` must run on it — on every path, including a card pull
//! mid-parse. A build that gets neither sits adopted (so it is found by lookups),
//! reasoned (so it can never be evicted) and at zero geometry, which makes every
//! fill-context consumer silently resolve nothing for that sample.
//!
//! That is why the adopted region lives in its own function,
//! [`drive_adopted_build`]: every `return` in it runs exactly one of the two, and
//! there is no path out of it that runs neither.
//!
//! The efatfs handle is the second resource, and it is released structurally the
//! same way: [`drive_adopted_build`] never closes it, and [`load_once`] closes it
//! exactly once on the single path that leads out of that call. The table holds
//! `efatfs_core::MAX_HANDLES` (128) slots and a resident `Sample` pins one for
//! its whole lifetime, so a leaked handle here is silent capacity loss, not a
//! recoverable error.
//!
//! ## Concurrency
//!
//! [`audio_load_task`] runs on the same executor as `streaming_fill_task` (its
//! own interrupt executor on device, its own thread on host), because like that
//! task it holds the single `efatfs` FS mutex across an SD await. A task that did
//! that on the thread-mode executor would deadlock against the storage fiber's
//! non-yielding `sim_block::block_on` spin, which cannot let this task run to
//! release the mutex.

// `LoadRequest::path` and the candidate list are heap strings: the path bound
// (`DELUGE_MAX_PATH`, 256) times the candidate bound (3) is 768 bytes, which is
// more than belongs in an interrupt-executor stack frame or a task future. This
// module compiles both `no_std` (device) and `std` (the `#[path]`-recompiled
// host test), and `extern crate` visibility is per-module in Rust 2018+, so this
// cannot rely on another module's `extern crate alloc;`.
extern crate alloc;

use alloc::string::String;
use core::ffi::{CStr, c_char};

use embassy_sync::blocking_mutex::raw::CriticalSectionRawMutex;
use embassy_sync::mutex::Mutex;
use embassy_sync::signal::Signal;

/// How many queued loads are held before further requests are dropped. A drop is
/// safe: the caller has already been told `LOAD_IN_PROGRESS` and its next cursor
/// move re-requests.
const LOAD_QUEUE_DEPTH: usize = 8;

/// Raised to wake [`audio_load_task`] out of its idle wait when a load is newly
/// enqueued. Always compiled (see the module doc's "Two compilation tiers") —
/// with the feature off, or before the task is spawned, nobody awaits it and
/// signalling it is a harmless flag set.
pub static LOAD_WAKE: Signal<CriticalSectionRawMutex, ()> = Signal::new();

/// One queued load. Only the path: candidates, open and size are all I/O and all
/// happen inside [`load_once`].
pub struct LoadRequest {
    pub path: String,
}

/// The bounded queue `deluge_audio_load_enqueue` pushes into and
/// [`audio_load_task`] drains. Duplicate paths are excluded upstream by the C++
/// in-flight table, so nothing here needs to de-duplicate.
static QUEUE: Mutex<CriticalSectionRawMutex, heapless::Deque<LoadRequest, LOAD_QUEUE_DEPTH>> =
    Mutex::new(heapless::Deque::new());

/// Whether the Rust async loader backs sample loads on this BSP/config — the
/// switch the C++ side asks before choosing the async path over the synchronous
/// one. The symbol itself must always exist so that call site links regardless
/// of which config produced this BSP image; only the return value moves.
///
/// Keyed on `async_audio_load_active`, NOT on `async_audio_load`, and the
/// separation is deliberate. `async_audio_load` says the driver is *compiled*
/// (which keeps the host test running under a plain `cargo test` and keeps the
/// device link honest); this says card samples actually *load through it*, which
/// is a behavioural change and must be an explicit, reversible flip rather than
/// a side effect of the code landing. Off by default, so a build that merely
/// contains this task still takes the synchronous path for every load.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_audio_load_async_active() -> bool {
    cfg!(feature = "async_audio_load_active")
}

/// Queue `file_path` for the async loader and wake it. See `audio_load.h`.
///
/// # Safety
///
/// `file_path` must be NULL or a valid NUL-terminated C string that stays valid
/// for the duration of this call. The path is copied out before this returns, so
/// the caller may free it immediately afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_audio_load_enqueue(file_path: *const c_char) {
    // SAFETY: the caller's contract, above.
    let Some(path) = (unsafe { cstr_to_string(file_path) }) else {
        return;
    };
    if let Ok(mut queue) = QUEUE.try_lock() {
        // A full queue drops the request: the caller has already been told
        // LOAD_IN_PROGRESS, and its next cursor move re-requests. `try_lock`
        // rather than a block because this is called synchronously from C++ in
        // whatever context the app happens to be in.
        let _ = queue.push_back(LoadRequest { path });
    }
    LOAD_WAKE.signal(());
}

/// Copy a C string out to an owned [`String`]. `None` for NULL or non-UTF-8.
///
/// # Safety
///
/// `ptr` must be NULL or a valid NUL-terminated C string.
unsafe fn cstr_to_string(ptr: *const c_char) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    // SAFETY: the caller's contract, above.
    let cstr = unsafe { CStr::from_ptr(ptr) };
    cstr.to_str().ok().map(String::from)
}

// ── The driver ──────────────────────────────────────────────────────────────

#[cfg(feature = "async_audio_load")]
mod driver {
    // `extern crate` visibility is per-module in Rust 2018+, so the outer
    // module's declaration is not in scope here.
    extern crate alloc;

    use super::LoadRequest;
    use alloc::string::String;
    use alloc::vec::Vec;
    use core::ffi::c_void;
    use deluge_audio_format::{
        AudioHeader, FeedResult, HeaderError, HeaderParser, MAX_WINDOW_BYTES,
    };

    /// Longest single candidate path, including its NUL — `DELUGE_MAX_PATH`
    /// (`audio_load.h`). The C side writes into `max_candidates *
    /// DELUGE_MAX_PATH` caller-owned bytes at this stride, so it is a layout
    /// constant, not a preference.
    pub const DELUGE_MAX_PATH: usize = 256;

    /// `deluge_audio_load_candidates`' "`out` is not trustworthy at all"
    /// sentinel (a candidate did not fit, or an internal allocation failed).
    /// Never a legitimate count.
    const DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED: u32 = 0xFFFF_FFFF;

    // Deluge `Error` codes as they cross the ABI, matched by NUMBER
    // (`audio_load.h`: "Fixed-width, never a native enum"). `enum class Error`
    // has no fixed underlying type, so the C++ side pins these numeric
    // identities with `static_assert`s in `audio_load_abi.cpp`; these constants
    // are this side of that same pinning.
    /// `Error::NONE`.
    pub const DELUGE_ERROR_NONE: u8 = 0;
    /// `Error::FILE_CORRUPTED`.
    pub const DELUGE_ERROR_FILE_CORRUPTED: u8 = 5;
    /// `Error::FILE_UNSUPPORTED`.
    const DELUGE_ERROR_FILE_UNSUPPORTED: u8 = 7;

    /// The parsed header geometry, as `deluge_audio_load_apply_geometry` takes
    /// it. Field-for-field `DelugeAudioLoadGeometry` (`audio_load.h`) — a named
    /// struct rather than 12 positional scalars precisely so a same-typed
    /// transposition cannot compile clean and render wrong.
    #[repr(C)]
    #[derive(Clone, Copy, Debug, Default, PartialEq)]
    pub struct LoadGeometry {
        pub sample_rate: u32,
        pub audio_data_start_pos_bytes: u32,
        pub audio_data_length_bytes: u32,
        pub file_loop_start_samples: u32,
        pub file_loop_end_samples: u32,
        pub wavetable_cycle_size: u32,
        /// Only meaningful when `midi_note_present` is nonzero.
        pub midi_note_value: f32,
        pub num_channels: u8,
        pub byte_depth: u8,
        /// `RawDataFormat`'s numeric value. An out-of-range byte is REFUSED by
        /// the C++ side, not degraded to a default.
        pub raw_data_format: u8,
        pub midi_note_present: u8,
        pub wavetable_explicitly_specifies_self: u8,
    }

    impl From<&AudioHeader> for LoadGeometry {
        /// Exactly the fields the synchronous `AudioFile::loadFile` writes onto a
        /// `Sample`. Anything missed here loads without error and renders wrong.
        fn from(header: &AudioHeader) -> Self {
            Self {
                sample_rate: header.sample_rate,
                audio_data_start_pos_bytes: header.audio_data_start_pos_bytes,
                audio_data_length_bytes: header.audio_data_length_bytes,
                file_loop_start_samples: header.file_loop_start_samples,
                file_loop_end_samples: header.file_loop_end_samples,
                wavetable_cycle_size: header.wavetable.cycle_size,
                // The C++ side turns "not present" into the -1.0f sentinel
                // `Sample::midiNoteFromFile` uses; this side only reports
                // presence, so the value carried alongside a zero flag is never
                // read.
                midi_note_value: header.midi_note.unwrap_or(0.0),
                num_channels: header.num_channels,
                byte_depth: header.byte_depth,
                raw_data_format: header.raw_data_format as u8,
                midi_note_present: u8::from(header.midi_note.is_some()),
                wavetable_explicitly_specifies_self: u8::from(
                    header.wavetable.explicitly_specifies_self,
                ),
            }
        }
    }

    /// Map a parser failure onto the Deluge `Error` the C++ side already
    /// branches on. Identical to the synchronous path's own mapping
    /// (`audio_file_format.cpp`'s `errorFromParseStatus`), which is what
    /// preserves the error identity `sample_browser.cpp` reads as "not a
    /// wavetable, try it as a sample".
    pub fn error_code_for(error: HeaderError) -> u8 {
        match error {
            // Neither RIFF/WAVE nor FORM/AIFF; or a bit depth, format tag,
            // channel count or sample rate we cannot play.
            HeaderError::NotAudio | HeaderError::Unsupported(_) => DELUGE_ERROR_FILE_UNSUPPORTED,
            // No fmt/COMM or no data/SSND chunk; or the file ends inside its own
            // header.
            HeaderError::Malformed(_) | HeaderError::Truncated { .. } => {
                DELUGE_ERROR_FILE_CORRUPTED
            }
        }
    }

    /// The FFI surface [`load_once`] needs, behind a trait so host tests can
    /// double it — see the module doc's "Why the driver is generic over
    /// `LoadOps`".
    ///
    /// The `*mut c_void` build values are opaque `DelugeSampleBuild*`s: this
    /// trait never interprets one, only threads it through, so a double can hand
    /// back whatever identity it likes.
    pub trait LoadOps {
        /// `deluge_audio_load_resolve_alternate_dir`. I/O: opens the alternate
        /// load dir if its status is still `MIGHT_EXIST`.
        ///
        /// MUST be awaited before [`LoadOps::candidates`]. While the status is
        /// unresolved the C++ side proposes only the regular path, so a
        /// collect-media song's first sample would resolve to a path its bytes
        /// are not at and silently fail to load where the synchronous path finds
        /// it. The outcome is deliberately not returned: a genuinely absent
        /// alternate dir is not a load failure, it just means the regular path
        /// is the only candidate, which is exactly what `candidates` then says.
        async fn ensure_alternate_dir_resolved(&self);

        /// `deluge_audio_load_candidates` — the paths to try, in order. No I/O.
        ///
        /// `None` means the C side reported `out` untrustworthy
        /// (`DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED`), and the load must fail
        /// rather than open whatever a silent truncation left behind. An empty
        /// list means there is nothing to try.
        fn candidates(&self, path: &str) -> Option<Vec<String>>;

        /// Open `path` for reading, resolving to an efatfs handle. `None` if it
        /// could not be opened (missing, unmounted, or the handle table is full).
        async fn open(&self, path: &str) -> Option<u32>;

        /// The size of the file behind `handle`, in bytes. Must read the same
        /// handle table [`LoadOps::open`] filled, or the parse gets a bound
        /// belonging to a different file.
        async fn size_of(&self, handle: u32) -> Option<u32>;

        /// `deluge_audio_load_begin`: allocate and adopt the `Sample`. Returns a
        /// null pointer on failure (nothing adopted, nothing to abandon).
        /// `alternate` is the empty string when the regular path won.
        fn begin(&self, path: &str, alternate: &str, size_bytes: u64) -> *mut c_void;

        /// `deluge_audio_load_bind_read_stream`: open the `Sample`'s OWN
        /// streaming read handle. Returns a Deluge `Error` code;
        /// [`DELUGE_ERROR_NONE`] on success.
        ///
        /// This is NOT the handle the header is parsed through. It goes through
        /// the `deluge_sample_stream` registry — a different handle space, and a
        /// scarce resource whose `TOO_MANY_OPEN_STREAMS` reclaim retry lives
        /// behind this call. A build published without it has its stream handle
        /// at 0: it loads with no error, appears in the browser, and plays
        /// silence forever.
        async fn bind_read_stream(&self, build: *mut c_void, path: &str) -> u8;

        /// Fill `buf` from absolute `offset` of the file behind `handle`.
        /// `false` if the full buffer could not be read.
        ///
        /// Never short at end of file, and that is load-bearing:
        /// `HeaderParser::new(size)` clamps every request so `start + len` is
        /// inside the file it was given the size of, and that size comes from
        /// [`LoadOps::size_of`] on this same handle. So a `false` here is a real
        /// read failure, never an end-of-file that a byte count would have had to
        /// distinguish.
        async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool;

        /// `deluge_audio_load_apply_geometry`: write the parsed geometry onto the
        /// build. `false` if it was REFUSED (an invalid `raw_data_format`, or an
        /// invalid/consumed build), in which case nothing was written and the
        /// build must be abandoned rather than finished.
        fn apply_geometry(&self, build: *mut c_void, geometry: &LoadGeometry) -> bool;

        /// `deluge_audio_load_finish`: apply the parse result, register the fill
        /// context, publish. Consumes the build. `error` is `Error::NONE` to
        /// publish, anything else to destroy. Returns whether it was published.
        fn finish(&self, build: *mut c_void, error: u8) -> bool;

        /// `deluge_audio_load_abandon`: destroy a build that will never carry a
        /// parse result. Consumes the build.
        fn abandon(&self, build: *mut c_void);

        /// Release the efatfs handle. The table holds only 128 slots and a
        /// resident `Sample` pins one for its whole lifetime, so a leak here is
        /// silent capacity loss.
        async fn close(&self, handle: u32);
    }

    /// Drive one queued load to completion; `true` if a `Sample` was published.
    ///
    /// The order is forced by the ABI rather than chosen: the alternate-dir
    /// status has to be resolved before candidates are asked for, and a candidate
    /// has to be opened and sized before `begin` can be called at all (it takes
    /// the resolved size and the winning path). See the module doc for how the
    /// two owned resources — the adopted build and the efatfs handle — are
    /// released structurally rather than by repeating the cleanup at each exit.
    pub async fn load_once<O: LoadOps>(ops: &O, request: LoadRequest) -> bool {
        // Discovery first: candidates are incomplete while the alternate-dir
        // status is MIGHT_EXIST.
        ops.ensure_alternate_dir_resolved().await;

        let Some(candidates) = ops.candidates(&request.path) else {
            return false; // `out` untrustworthy — fail rather than open a truncated path.
        };

        // One open for the whole load. The C++ side supplies only the policy
        // (which paths, in what order); trying them is I/O and belongs here.
        // This is also what removes the synchronous path's double open, where
        // resolving the size opened the file just to close it again.
        let mut opened: Option<(u32, String)> = None;
        for candidate in candidates {
            if let Some(handle) = ops.open(&candidate).await {
                opened = Some((handle, candidate));
                break;
            }
        }
        let Some((handle, winning_path)) = opened else {
            return false; // Nothing opened: no build exists yet, so there is nothing to abandon.
        };

        let published = match ops.size_of(handle).await {
            Some(size_bytes) => {
                // `usingAlternateLocation` is empty exactly when the regular path won.
                let alternate = if winning_path == request.path {
                    ""
                } else {
                    winning_path.as_str()
                };
                let build = ops.begin(&request.path, alternate, u64::from(size_bytes));
                if build.is_null() {
                    // `begin` destroyed its own object on failure; nothing was adopted.
                    false
                } else {
                    drive_adopted_build(ops, build, handle, size_bytes, &winning_path).await
                }
            }
            None => false,
        };

        // The single close, on the one path out of everything above.
        ops.close(handle).await;
        published
    }

    /// Everything that happens while a build is adopted. Every `return` runs
    /// exactly one of `finish` or `abandon`; nothing here closes `handle` (see
    /// [`load_once`]).
    ///
    /// `size_bytes` is the file's real size and `path` the path it was opened
    /// at — the same path the Sample's own read stream is bound to.
    async fn drive_adopted_build<O: LoadOps>(
        ops: &O,
        build: *mut c_void,
        handle: u32,
        size_bytes: u32,
        path: &str,
    ) -> bool {
        // Bind the Sample's own read stream before anything can publish it.
        // Separate handle space from `handle`; without this the sample loads
        // cleanly and plays silence.
        if ops.bind_read_stream(build, path).await != DELUGE_ERROR_NONE {
            ops.abandon(build);
            return false;
        }

        let mut parser = HeaderParser::new(size_bytes);
        let mut window = [0u8; MAX_WINDOW_BYTES as usize];
        while let Some((start, len)) = parser.pending() {
            let len = len as usize;
            if len > window.len() {
                // Unreachable by contract: `MAX_WINDOW_BYTES` is a compile-time
                // promise the parser checks over every window it can ask for.
                // Refuse rather than panic if it is ever broken — a slice out of
                // range would take the whole executor down.
                ops.abandon(build);
                return false;
            }
            // A zero-length request is legitimate: `pending` clamps `len` to what
            // remains of the file, so a walk positioned at or past the end asks
            // for nothing. It must still be READ and FED — the parser resolves
            // the end of the file inside `feed`, and never terminates without
            // one, so skipping a zero-length request stalls this loop forever.
            // A zero-length read is a no-op that trivially succeeds.
            if !ops.read_at(handle, start, &mut window[..len]).await {
                ops.abandon(build);
                return false;
            }
            match parser.feed(&window[..len], start) {
                Ok(FeedResult::Done(header)) => {
                    let geometry = LoadGeometry::from(&header);
                    if !ops.apply_geometry(build, &geometry) {
                        // Refused, so nothing was written and the build is still
                        // at zero geometry. Publishing it would be the silent
                        // wrong-render this refusal exists to prevent.
                        ops.abandon(build);
                        return false;
                    }
                    return ops.finish(build, DELUGE_ERROR_NONE);
                }
                Ok(FeedResult::Need { .. }) => continue,
                Err(error) => {
                    // A parse RESULT exists, so this goes through `finish` (which
                    // runs the same destroy path the synchronous parse failure
                    // did) rather than `abandon`.
                    ops.finish(build, error_code_for(error));
                    return false;
                }
            }
        }

        // `pending` went None without a `Done`: unreachable for a fresh parser
        // (its first phase is always non-terminal, and every terminal phase is
        // reported through `feed`), but a walk that ends with no header is still
        // a build nobody will ever finish.
        ops.abandon(build);
        false
    }

    /// Decode what `deluge_audio_load_candidates` wrote into `scratch`, given the
    /// `count` it returned.
    ///
    /// `count` is a total, snprintf-style: it CAN exceed the capacity it was
    /// given, meaning only the first `capacity` slots were actually written. Only
    /// those are read — iterating `0..count` would hand back whatever was in the
    /// caller's buffer for the slots the C side never touched.
    pub fn decode_candidates(scratch: &[u8], count: u32) -> Option<Vec<String>> {
        if count == DELUGE_AUDIO_LOAD_CANDIDATES_TRUNCATED {
            return None;
        }
        let capacity = scratch.len() / DELUGE_MAX_PATH;
        let written = (count as usize).min(capacity);
        let mut out = Vec::with_capacity(written);
        for slot in 0..written {
            let bytes = &scratch[slot * DELUGE_MAX_PATH..(slot + 1) * DELUGE_MAX_PATH];
            // Each slot holds one NUL-terminated path; anything after the NUL is
            // untouched scratch.
            let end = bytes.iter().position(|b| *b == 0)?;
            out.push(String::from(core::str::from_utf8(&bytes[..end]).ok()?));
        }
        Some(out)
    }
}

// Re-exported for `tests/audio_loader_host.rs`, which recompiles this file
// through `#[path]`: `driver` itself is private, and the bin target needs only
// `load_once` of these, so the rest read as unused here.
#[cfg(feature = "async_audio_load")]
#[allow(unused_imports)]
pub use driver::{LoadGeometry, LoadOps, decode_candidates, load_once};

// ── Production wiring ───────────────────────────────────────────────────────
// Only compiled where the real C-ABI symbols and the efatfs backing are actually
// linked: on device under `efatfs_streaming`, and on host under `host_app`. Kept
// separate from the driver above (which compiles under `async_audio_load` alone)
// so the `#[path]`-recompiled host test never emits a reference to an undefined
// extern symbol.

#[cfg(all(
    feature = "async_audio_load",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
mod prod {
    use super::driver::{
        DELUGE_ERROR_FILE_CORRUPTED, DELUGE_MAX_PATH, LoadGeometry, LoadOps, decode_candidates,
    };
    // Per-module, as in `driver` above.
    extern crate alloc;

    use alloc::ffi::CString;
    use alloc::string::String;
    use alloc::vec::Vec;
    use core::ffi::{c_char, c_void};

    unsafe extern "C" {
        fn deluge_audio_load_begin(
            file_path: *const c_char,
            alternate_path: *const c_char,
            size_bytes: u64,
            error_out: *mut u8,
        ) -> *mut c_void;
        fn deluge_audio_load_resolve_alternate_dir(error_out: *mut u8) -> bool;
        fn deluge_audio_load_candidates(
            file_path: *const c_char,
            out: *mut c_char,
            max_candidates: u32,
        ) -> u32;
        fn deluge_audio_load_bind_read_stream(build: *mut c_void, path: *const c_char) -> u8;
        fn deluge_audio_load_apply_geometry(
            build: *mut c_void,
            geometry: *const LoadGeometry,
            error_out: *mut u8,
        ) -> bool;
        fn deluge_audio_load_finish(build: *mut c_void, error_in: u8, error_out: *mut u8) -> bool;
        fn deluge_audio_load_abandon(build: *mut c_void);
    }

    /// The real [`LoadOps`], wired to `libdeluge/audio_load.h` and the efatfs
    /// streaming handle table (`crate::efatfs_fs` / `crate::efatfs_host_shim`).
    pub struct ProdOps;

    impl ProdOps {
        pub fn new() -> Self {
            Self
        }
    }

    /// `AudioFileManager::kMaxLoadCandidates` — the C side proposes at most this
    /// many, so a larger buffer would only ever be partly written.
    const MAX_LOAD_CANDIDATES: usize = 3;

    /// Every `const char*` this ABI takes is documented "must not be NULL", so a
    /// path that cannot be turned into a C string (an interior NUL) has to fail
    /// the call rather than pass NULL through.
    fn to_cstring(path: &str) -> Option<CString> {
        CString::new(path).ok()
    }

    /// Scratch big enough for `deluge_audio_load_candidates` to write
    /// [`MAX_LOAD_CANDIDATES`] slots at a `DELUGE_MAX_PATH` stride. Heap, not
    /// stack: this runs on an interrupt-executor stack on device, where 768 bytes
    /// of frame is not free.
    fn candidate_scratch() -> Vec<u8> {
        alloc::vec![0u8; MAX_LOAD_CANDIDATES * DELUGE_MAX_PATH]
    }

    impl LoadOps for ProdOps {
        async fn ensure_alternate_dir_resolved(&self) {
            // Speculative-safe: a no-op doing no I/O once the status is already
            // resolved. The verdict is deliberately dropped — see the trait doc.
            // SAFETY: a NULL `error_out` is explicitly allowed.
            let _ = unsafe { deluge_audio_load_resolve_alternate_dir(core::ptr::null_mut()) };
        }

        fn candidates(&self, path: &str) -> Option<Vec<String>> {
            let c_path = to_cstring(path)?;
            let mut scratch = candidate_scratch();
            let capacity = (scratch.len() / DELUGE_MAX_PATH) as u32;
            // SAFETY: `c_path` is a valid NUL-terminated C string; `scratch` is
            // `capacity * DELUGE_MAX_PATH` caller-owned bytes, exactly what the
            // header asks for, and is not aliased for the duration of the call.
            let count = unsafe {
                deluge_audio_load_candidates(
                    c_path.as_ptr(),
                    scratch.as_mut_ptr().cast::<c_char>(),
                    capacity,
                )
            };
            decode_candidates(&scratch, count)
        }

        async fn open(&self, path: &str) -> Option<u32> {
            match backing::open(path).await {
                crate::efatfs_core::OpenOutcome::Handle(handle) => Some(handle),
                // Missing, unmounted, or the handle table is full. All three end
                // this candidate; the next one (if any) is tried.
                _ => None,
            }
        }

        async fn size_of(&self, handle: u32) -> Option<u32> {
            backing::size_of_handle(handle).await
        }

        fn begin(&self, path: &str, alternate: &str, size_bytes: u64) -> *mut c_void {
            let (Some(c_path), Some(c_alternate)) = (to_cstring(path), to_cstring(alternate))
            else {
                return core::ptr::null_mut();
            };
            // SAFETY: both pointers are valid NUL-terminated C strings (the
            // header requires non-NULL, and "" for "no alternate location"); a
            // NULL `error_out` is explicitly allowed, and a NULL return is still
            // NULL without it.
            unsafe {
                deluge_audio_load_begin(
                    c_path.as_ptr(),
                    c_alternate.as_ptr(),
                    size_bytes,
                    core::ptr::null_mut(),
                )
            }
        }

        async fn bind_read_stream(&self, build: *mut c_void, path: &str) -> u8 {
            let Some(c_path) = to_cstring(path) else {
                return DELUGE_ERROR_FILE_CORRUPTED;
            };
            // SAFETY: `build` is a live build from `begin`, not yet consumed;
            // `c_path` is a valid NUL-terminated C string.
            unsafe { deluge_audio_load_bind_read_stream(build, c_path.as_ptr()) }
        }

        async fn read_at(&self, handle: u32, offset: u32, buf: &mut [u8]) -> bool {
            backing::read_at(handle, offset, buf).await
        }

        fn apply_geometry(&self, build: *mut c_void, geometry: &LoadGeometry) -> bool {
            // SAFETY: `build` is a live build from `begin`; `geometry` is a live
            // `#[repr(C)]` value laid out field-for-field as
            // `DelugeAudioLoadGeometry`, and the callee only reads it. A NULL
            // `error_out` is allowed — the bool alone distinguishes refusal.
            unsafe { deluge_audio_load_apply_geometry(build, geometry, core::ptr::null_mut()) }
        }

        fn finish(&self, build: *mut c_void, error: u8) -> bool {
            // SAFETY: `build` is a live build from `begin`, consumed by this call
            // and never touched again. A NULL `error_out` is allowed.
            unsafe { deluge_audio_load_finish(build, error, core::ptr::null_mut()) }
        }

        fn abandon(&self, build: *mut c_void) {
            // SAFETY: `build` is a live build from `begin`, consumed by this call
            // and never touched again. NULL/already-consumed is a documented
            // no-op.
            unsafe { deluge_audio_load_abandon(build) };
        }

        async fn close(&self, handle: u32) {
            backing::close(handle).await;
        }
    }

    /// The efatfs handle table behind the production ops: the device mount on
    /// device, the host shim under `host_app`. One alias rather than a `#[cfg]`
    /// at every call site.
    #[cfg(target_os = "none")]
    use crate::efatfs_fs as backing;
    #[cfg(not(target_os = "none"))]
    use crate::efatfs_host_shim as backing;
}

#[cfg(all(
    feature = "async_audio_load",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
pub use prod::ProdOps;

/// The load task: wakes on [`LOAD_WAKE`], drains every queued load through the
/// real [`ProdOps`], then goes back to sleep.
///
/// Waits BEFORE constructing [`ProdOps`], for the same reason
/// `streaming_fill_task` does: nothing can signal [`LOAD_WAKE`] until the C++
/// enqueue path runs, which is necessarily after `deluge_app_init`, and
/// [`LOAD_WAKE`] is a latching `Signal` so a wake that arrives before this task
/// is first polled is stored and returned immediately rather than lost.
#[cfg(all(
    feature = "async_audio_load",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
#[embassy_executor::task]
pub async fn audio_load_task() {
    LOAD_WAKE.wait().await;
    let ops = ProdOps::new();
    loop {
        while let Some(request) = next_queued_request().await {
            load_once(&ops, request).await;
        }
        LOAD_WAKE.wait().await;
    }
}

/// Pop the next queued request, or `None` when the queue is empty.
#[cfg(all(
    feature = "async_audio_load",
    any(
        all(target_os = "none", feature = "efatfs_streaming"),
        feature = "host_app"
    )
))]
async fn next_queued_request() -> Option<LoadRequest> {
    QUEUE.lock().await.pop_front()
}
