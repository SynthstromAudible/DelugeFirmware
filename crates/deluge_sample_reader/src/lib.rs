//! The sample range-reader C ABI (`libdeluge/sample_reader.h`).
//!
//! A zero-copy reader handle over a sample's source residency, for non-voice consumers that read
//! raw-PCM frame ranges without touching `StreamedChunk` internals: the non-voice twin of
//! `deluge_sample_source`'s region port. The stateless `deluge_sample_read` copy is composed from
//! the same handle (open, window/advance, drop), so there is one frame-mapping and fill
//! implementation.
#![no_std]

extern crate alloc;

pub mod abi;
pub mod reader;
pub mod reservation;

// Host stand-ins for the C-ABI symbols `deluge_resource` calls (critical section, interrupt
// queries, clock). The BSP provides them in the firmware; `deluge_resource`'s own test stubs are
// private to its crate, so this test binary must define them or every test that drops a `Lease`
// fails to link.
#[cfg(test)]
mod host_critical_section_stubs {
    extern crate std;
    use core::cell::Cell;

    std::thread_local! {
        static DEPTH: Cell<u32> = const { Cell::new(0) };
        static TOKEN: Cell<Option<critical_section::RestoreState>> = const { Cell::new(None) };
        static FAKE_NOW: Cell<u32> = const { Cell::new(1) };
    }

    /// The loader-latency clock (`src/deluge/io/debug/resource_clock.cpp` in the firmware). A
    /// monotonic counter meets the manager's only requirement: reads never go backwards.
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

    // These tests never model the audio ISR, so both context queries answer `false`.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_interrupt() -> bool {
        false
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_audio_context() -> bool {
        false
    }
}

// Host stand-ins for the streaming externs (card read, fill signal and request, manager lookup),
// shared by every test module: a `#[no_mangle]` symbol may be defined only once per binary.
//
// The `deluge_sample_fill::chunk` accessors are plain `pub fn`s, so there is nothing to stub: tests
// construct real `StreamedChunk` backings (see `reader::tests::window_tests::real_chunk_construct`)
// and read them through the real accessors. A mock could not match `StreamedChunk`'s non-`repr(C)`
// layout, and reborrowing an unconstructed backing as `&StreamedChunk` is UB.
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

    /// Serializes tests that register fill-contexts.
    ///
    /// `deluge_sample_fill`'s `FILL_CONTEXTS` table is process-wide and keyed by bare asset id,
    /// while every test's fresh manager hands out ids from 0, so concurrent tests would overwrite
    /// each other's contexts. Every test that calls `deluge_streaming_set_fill_context` or
    /// `Reader::open` must hold this lock throughout.
    pub(crate) static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    /// Points `deluge_streaming_resource_manager()` at `handle` for the calling thread.
    pub(crate) fn set_active_manager(handle: *mut c_void) {
        ACTIVE_MANAGER.with(|m| m.set(handle));
    }

    /// Makes every `deluge_efatfs_read_at` call on this thread fail until cleared. To fail a single
    /// cluster instead, use [`set_fail_at_byte_offset`].
    pub(crate) fn set_force_read_failure(force: bool) {
        FORCE_READ_FAILURE.with(|f| f.set(force));
    }

    /// Makes `deluge_efatfs_read_at` fail only the call whose `byte_offset` equals `offset`
    /// (`cluster_index << cluster_size_magnitude`); `None` clears it.
    ///
    /// This isolates a failing neighbour cluster while the reader's own cluster still fills, which
    /// is where the straddle degrade paths live.
    pub(crate) fn set_fail_at_byte_offset(offset: Option<u32>) {
        FAIL_AT_BYTE_OFFSET.with(|f| f.set(offset));
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_resource_manager() -> *mut c_void {
        ACTIVE_MANAGER.with(|m| m.get())
    }

    /// No-op: the test binary has no async loader to wake.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_signal_fill() {}

    /// Runs the fill in place, as the test binary has no async fill task, so
    /// `Reader::acquire_and_fill` finds the chunk ready on the same call.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_request_chunk(
        chunk_backing: *mut deluge_sample_fill::chunk::DelugeChunkBacking,
    ) {
        // SAFETY: the reader requests fills only for chunks it holds a lease on.
        let _ = unsafe { crate::reservation::fill_now(chunk_backing.cast()) };
    }

    /// Synthetic card read: `dst[i] = (byte_offset + i) as u8`, so a test can compute a cluster's
    /// expected bytes independently.
    ///
    /// The injected failures return `DELUGE_ERR_IO` before touching `dst`/`out_read`, so they apply
    /// even to a zero-length read.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_efatfs_read_at(
        _handle: deluge_sample_fill::DelugeHandle,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> deluge_sample_fill::DelugeStatus {
        // `DELUGE_ERR_IO`: what the real read answers for a failed card read.
        const DELUGE_ERR_IO: deluge_sample_fill::DelugeStatus = -5;
        if FORCE_READ_FAILURE.with(|f| f.get()) {
            return DELUGE_ERR_IO;
        }
        if FAIL_AT_BYTE_OFFSET.with(|f| f.get()) == Some(byte_offset) {
            return DELUGE_ERR_IO;
        }
        // SAFETY: the only caller, `fill_now`, passes `dst` valid for `count` bytes and a valid
        // `out_read`.
        unsafe {
            let d = dst as *mut u8;
            for i in 0..count {
                *d.add(i as usize) = byte_offset.wrapping_add(i) as u8;
            }
            *out_read = count;
        }
        deluge_sample_fill::DELUGE_OK
    }
}
