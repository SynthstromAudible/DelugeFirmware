//! deluge_sample_reader — the sample range-reader C-ABI (`include/libdeluge/sample_reader.h`).
//!
//! A zero-copy streaming reader-handle plus a stateless copy convenience over a sample's source
//! residency, for non-voice consumers that read raw-PCM frame RANGES instead of reaching
//! `StreamedChunk` internals the way they do today — the non-voice twin of the voice region port
//! (`deluge_sample_source`'s `DelugeSampleSource`/`DelugeSampleRegion`). Coexists with the existing
//! facade (`peek`/`prefetch`/`load_now`/`request`/`dequeue`); no consumer migrates yet — see the
//! design doc this crate's Cargo.toml references.
//!
//! The lifecycle trio (`open`/`seek`/`close`) and the read core (`window`/`advance`/`ok`, plus
//! `open`'s own Rust-side geometry resolution) share one handle with the stateless
//! `deluge_sample_read` copy, which composes entirely from that same handle (`open` ->
//! `window`/`advance` -> drop) — no second frame-mapping/fill implementation.
#![no_std]

extern crate alloc;

pub mod abi;
pub mod reader;
pub mod reservation;

// `deluge_resource::sync` calls out to three C-ABI critical-section primitives that, in the real
// firmware/host-sim link, the BSP (`src/bsp/rust/src/services.rs`) provides. `deluge_resource`'s
// own `cargo test` binary supplies host stubs for them internally (`sync::stubs`,
// `#[cfg(test)]`-gated and crate-private), but that module isn't visible to downstream crates — so
// this crate's own test binary, which links `deluge_resource` as a plain (non-test) rlib, must
// provide the same three symbols itself or every test that drops a `Lease` (which goes through
// `Masked`) fails to link. Mirrors `deluge_sample_source::lib`'s own
// `host_critical_section_stubs` — the identical problem, the identical fix.
#[cfg(test)]
mod host_critical_section_stubs {
    extern crate std;
    use core::cell::Cell;

    std::thread_local! {
        static DEPTH: Cell<u32> = const { Cell::new(0) };
        static TOKEN: Cell<Option<critical_section::RestoreState>> = const { Cell::new(None) };
        static FAKE_NOW: Cell<u32> = const { Cell::new(1) };
    }

    /// The clock `deluge_resource` measures loader service latency with. In the firmware the app
    /// provides it (`src/deluge/io/debug/resource_clock.cpp`, returning
    /// `AudioEngine::audioSampleTimer`); a host test binary links the manager without the app, so it
    /// must supply one or the reference from `Manager::loader_next` fails to link. A monotonic counter
    /// satisfies the manager's only requirement — successive reads must not go backwards.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_debug_now_frames() -> u32 {
        FAKE_NOW.with(|c| {
            let n = c.get().wrapping_add(1);
            c.set(n);
            n
        })
    }

    #[unsafe(no_mangle)]
    extern "C" fn ENTER_CRITICAL_SECTION() {
        DEPTH.with(|d| {
            if d.get() == 0 {
                // SAFETY: released by the matching EXIT once this thread's depth hits 0.
                let t = unsafe { critical_section::acquire() };
                TOKEN.with(|tok| tok.set(Some(t)));
            }
            d.set(d.get() + 1);
        });
    }

    #[unsafe(no_mangle)]
    extern "C" fn EXIT_CRITICAL_SECTION() {
        let closed = DEPTH.with(|d| {
            let n = d.get().saturating_sub(1);
            d.set(n);
            n == 0
        });
        if closed {
            TOKEN.with(|tok| {
                if let Some(t) = tok.take() {
                    // SAFETY: stashed by this thread's outermost ENTER, above.
                    unsafe { critical_section::release(t) };
                }
            });
        }
    }

    // This crate's tests are single-threaded and never model the audio-ISR context (that
    // asymmetry is `deluge_resource`'s own concern, proved in its crate) — so "never in an
    // interrupt" is the right constant answer here.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_interrupt() -> bool {
        false
    }

    // Same reasoning as `deluge_in_interrupt` above: never the audio context here, so
    // `deluge_resource`'s mask is always taken under this crate's tests.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_audio_context() -> bool {
        false
    }
}

// `reader.rs`'s `fill_now`/`window()` reach the synchronous card read through an
// `unsafe extern "C"` declaration — a real C++ symbol in production (`efatfs_fs.rs`), which this
// crate's own `cargo test` binary does not link. Mirrors `host_critical_section_stubs` above: ONE
// crate-level `#[cfg(test)]` module providing every stub `#[unsafe(no_mangle)]` definition this
// crate's test binary needs (never duplicated per-test-module — a `#[no_mangle]` symbol may only
// be defined once in a linked binary), used by both `reader::tests` and `abi::tests`.
//
// Does NOT stub the chunk field accessors: `deluge_sample_fill::chunk::payload`/
// `set_loaded`/`loaded`/`unloadable`/`set_unloadable`/`convert_state`/`set_convert_state` are plain
// `pub fn`s with no `#[no_mangle]` C-ABI wrapper, unconditionally compiled into
// every binary that links that crate (this crate always does — see its own `Cargo.toml`) — there is
// no C-ABI symbol left to shadow, so a test just calls them directly by path instead of stubbing.
// `reader.rs`'s own tests instead construct genuine `StreamedChunk` backings (via
// `deluge_sample_fill::chunk::deluge_streaming_chunk_construct`, registered as the asset's own
// construct callback — see `reader::tests::window_tests::real_chunk_construct`) and read them
// back through the real accessors, which is both simpler than shadowing them AND sound — a mock
// keyed on a hand-picked `PAYLOAD_OFFSET` couldn't ever match `StreamedChunk`'s own (Rust-chosen,
// not `repr(C)`) layout, and reborrowing a never-constructed backing as `&StreamedChunk` is UB
// regardless of whether the mock happens to agree with it.
#[cfg(test)]
pub(crate) mod host_streaming_stubs {
    extern crate std;
    use core::cell::Cell;
    use core::ffi::c_void;

    std::thread_local! {
        static ACTIVE_MANAGER: Cell<*mut c_void> = const { Cell::new(core::ptr::null_mut()) };
        static FORCE_READ_FAILURE: Cell<bool> = const { Cell::new(false) };
        static FAIL_AT_BYTE_OFFSET: Cell<Option<u32>> = const { Cell::new(None) };
    }

    /// Serializes every test that touches `deluge_sample_fill`'s per-asset fill-context table
    /// (`FILL_CONTEXTS`) — a GLOBAL, process-wide static keyed by bare asset id, NOT scoped per
    /// manager instance. Since every test builds its own fresh manager, and `Manager::define_asset`
    /// hands out ids starting from 0 for each one, two tests running concurrently (the `cargo test`
    /// default) can easily both define asset id 0 in their own manager and then race registering
    /// DIFFERENT fill-contexts for that same global slot. Mirrors
    /// `deluge_sample_source::abi::tests`'s own `TEST_LOCK` for its shared `POOL`/`ACTIVE_MANAGER`
    /// statics — the identical hazard, the identical fix. Every test that calls
    /// `deluge_streaming_set_fill_context`/`Reader::open` (both `reader::tests` and `abi::tests`)
    /// must take this lock for its whole run.
    pub(crate) static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    /// Route `deluge_streaming_resource_manager()` (below) to `handle` for the calling thread —
    /// `cargo test`'s default per-test thread gives each test its own manager without a shared
    /// mutex (unlike `deluge_sample_source::abi`'s tests, which serialize over real `static`s;
    /// this crate's manager is built fresh per test, so plain `thread_local` suffices).
    pub(crate) fn set_active_manager(handle: *mut c_void) {
        ACTIVE_MANAGER.with(|m| m.set(handle));
    }

    /// Make the next (and every subsequent, until reset) `deluge_efatfs_read_at` call on this
    /// thread fail without writing `dst` — the forced-failure half of the self-pin/failure test
    /// matrix (`reader::tests`'s `reader_ok` coverage). Fails EVERY read, regardless of which
    /// cluster — for isolating a single cluster's failure instead (the degrade-path tests, where
    /// the CURRENT cluster must succeed and only the NEIGHBOUR must fail), see
    /// [`set_fail_at_byte_offset`].
    pub(crate) fn set_force_read_failure(force: bool) {
        FORCE_READ_FAILURE.with(|f| f.set(force));
    }

    /// Make `deluge_efatfs_read_at` fail ONLY the call whose `byte_offset` param equals
    /// `offset` (i.e. `Some(cluster_index << cluster_size_magnitude)` — the exact value
    /// `fill_logic::begin`/`native_begin` compute for that cluster, see `read_source.cpp`'s own
    /// `byte_offset = cluster_index << cluster_size_magnitude`), leaving every other cluster's
    /// read to succeed normally. `None` clears the injection. Lets a test isolate "this reader's
    /// own (self-pinned) cluster succeeds, but the NEIGHBOUR cluster `window()` tries to
    /// transiently fill for a boundary stitch fails" — the exact scenario the straddle
    /// degrade-path bugs live in, which the single global [`set_force_read_failure`] flag can't
    /// reach (it fails every cluster, including the one under test itself).
    pub(crate) fn set_fail_at_byte_offset(offset: Option<u32>) {
        FAIL_AT_BYTE_OFFSET.with(|f| f.set(offset));
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_resource_manager() -> *mut c_void {
        ACTIVE_MANAGER.with(|m| m.get())
    }

    // `deluge_sample_fill::chunk::payload`/`set_loaded`/`loaded`/`unloadable`/`set_unloadable`/
    // `convert_state`/`set_convert_state` are deliberately NOT stubbed here — see the module
    // doc for why: they are plain `pub fn`s on `deluge_sample_fill::chunk` (no `#[no_mangle]` C-ABI
    // wrapper), so there is nothing to shadow. Tests that need them go through the
    // real accessors directly over a genuinely constructed `StreamedChunk` backing.

    /// No-op stand-in for the real async-fill wake signal (`streaming_fill.h`'s
    /// `deluge_streaming_signal_fill`) — this test binary has no async loader task to wake;
    /// `reservation.rs`'s enqueue paths call this unconditionally after `loader_enqueue`, so it
    /// must exist for the host test binary to link.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_signal_fill() {}

    /// The host test binary has no async streaming-fill task, so it takes the synchronous `fill_now`
    /// route through `deluge_efatfs_read_at` above (the one these tests exercise) — exactly the
    /// C-host `deluge_render` path. `Reader::acquire_and_fill` gates on this.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_async_active() -> bool {
        false
    }

    /// Never reached in the host test binary (`deluge_streaming_async_active` returns false, so
    /// `acquire_and_fill` takes the `fill_now` branch), but declared as an `extern` there, so it
    /// must exist for the binary to link. Returns false.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_fill_chunk_blocking(_chunk_backing: *mut c_void) -> bool {
        false
    }

    /// Synthetic card read: deterministic content keyed on the ABSOLUTE file byte offset
    /// (`dst[i] = (byte_offset + i) as u8`), so a test can compute a cluster's expected
    /// post-fill bytes independently of this stub. `set_force_read_failure(true)` makes EVERY
    /// call fail closed (returns `false`, `dst`/`out_read` untouched); `set_fail_at_byte_offset`
    /// fails only the ONE cluster whose `byte_offset` matches, for isolating a single neighbour's
    /// failure. Both checks run before touching `count`/`dst`, so they apply even to a
    /// zero-sector (`count == 0`) read.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_efatfs_read_at(
        _handle: u32,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> bool {
        if FORCE_READ_FAILURE.with(|f| f.get()) {
            return false;
        }
        if FAIL_AT_BYTE_OFFSET.with(|f| f.get()) == Some(byte_offset) {
            return false;
        }
        // SAFETY: `dst` is the caller's just-allocated destination buffer, valid for at least
        // `count` bytes (this crate's own `fill_now`, the only caller); `out_read` is a valid
        // out-param.
        unsafe {
            let d = dst as *mut u8;
            for i in 0..count {
                *d.add(i as usize) = byte_offset.wrapping_add(i) as u8;
            }
            *out_read = count;
        }
        true
    }
}
