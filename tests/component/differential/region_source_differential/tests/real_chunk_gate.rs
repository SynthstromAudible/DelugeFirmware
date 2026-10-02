//! Payload-offset gate: the region cursor (`SampleSource<ManagerResidency>`) over a real
//! `deluge_resource` manager whose chunks are real `StreamedChunk`s.
//!
//! A streamed chunk's payload sits at a non-zero offset from its manager backing, behind the
//! `StreamedChunk` header. Payloads are seeded with `make_ramp` through
//! `deluge_sample_fill::chunk::payload`, so a cursor that reads from the backing pointer instead
//! (as `ManagerPin::payload()` once did) sees header bytes and fails the ramp comparison. A fake
//! backing with `payload == backing` cannot catch that.
//!
//! The cursor is built directly (`ManagerResidency::new` + `SampleSource::new`) rather than through
//! the `deluge_sample_source_open` C-ABI bridge, which would need more test doubles and adds
//! nothing to the payload-offset question.
use core::ffi::c_void;
use std::ptr;
use std::sync::Mutex;

use deluge_resource::value::COST_IO;
use deluge_resource::{
    deluge_resource_create, deluge_resource_define_asset, deluge_resource_lease_count_by_slot,
    deluge_resource_mark_ready, deluge_resource_release, deluge_resource_request,
    deluge_resource_set_construct, deluge_resource_try_acquire, manager::BACKING_HEAP,
    DelugeResource,
};
use deluge_sample_source::cursor::{RegionOut, RegionState, SampleSource};
use deluge_sample_source::geometry::Geometry;
use deluge_sample_source::manager_residency::ManagerResidency;

// `deluge_resource::sync::Masked` calls three `extern "C"` critical-section primitives. The
// dependency crates' own `#[cfg(test)]` stubs are compiled out here, so this binary supplies them.
// `deluge_in_interrupt` is always false, so masking is really exercised.
mod host_critical_section_stubs {
    use std::cell::Cell;

    std::thread_local! {
        static DEPTH: Cell<u32> = const { Cell::new(0) };
        static TOKEN: Cell<Option<critical_section::RestoreState>> = const { Cell::new(None) };
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

    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_interrupt() -> bool {
        false
    }
    // Never on the audio context here; the timer only feeds latency stats nothing reads.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_audio_context() -> bool {
        false
    }
    #[unsafe(no_mangle)]
    extern "C" fn deluge_debug_now_frames() -> u32 {
        0
    }
}

/// Link stub for the fill signal `ManagerResidency::acquire`'s loading branch sends. No fill task
/// runs here (every index is made ready up front), and a loading acquire, such as an unseeded
/// prefetch neighbour, must not stall, so a no-op is correct.
#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_signal_fill() {}

/// Link stub for `deluge_sample_stream_asset_id`, used only by the unused `open()` bridge in
/// `abi.rs`; present in case that code is linked in. Never called.
#[unsafe(no_mangle)]
extern "C" fn deluge_sample_stream_asset_id(
    _stream_backing: *mut deluge_sample_source::abi::DelugeStreamBacking,
) -> u32 {
    0
}

/// Construct callback forwarding to `deluge_sample_fill::chunk::deluge_streaming_chunk_construct`,
/// the one production uses for streamed sample chunks.
///
/// Needed only because `ConstructFn` takes `dest: *mut u8` and the target takes `*mut c_void`;
/// Rust function-pointer types don't unify the two.
unsafe extern "C" fn construct_streamed_chunk(
    ctx: *mut c_void,
    owner: *mut c_void,
    index: u32,
    dest: *mut u8,
) {
    // SAFETY: forwards `dest` unchanged under the construct contract `deluge_resource_request`
    // upholds for this callback.
    unsafe {
        deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
            ctx,
            owner,
            index,
            dest as *mut c_void,
        );
    }
}

/// Serializes tests: the manager's masking and the critical-section stubs are process-global.
static TEST_LOCK: Mutex<()> = Mutex::new(());

const CLUSTER_SIZE: u32 = 64;
const CHUNK_CAP: u32 = 32; // ample for this gate's handful of indices

/// A real `deluge_resource` manager over a test heap, with one asset whose `construct` places a
/// real streamed chunk (payload at `backing + payload_offset`) at each slab slot.
struct ChunkHarness {
    handle: *mut DelugeResource,
    asset: u32,
    backing_size: usize,
    _buf: Vec<u128>,
}

impl ChunkHarness {
    fn new() -> Self {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf = vec![0u128; words];
        // SAFETY: `buf` is a 16-aligned `words * 16`-byte arena kept alive by the harness.
        let heap = unsafe { fs_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        // SAFETY: `heap` is the live handle just created above.
        let handle = unsafe { deluge_resource_create(heap, 4, CHUNK_CAP as usize) };
        assert!(!handle.is_null());
        // SAFETY: `handle` is live for the duration of this call.
        let asset = unsafe {
            deluge_resource_define_asset(
                handle,
                ptr::null_mut(),
                None,
                None,
                ptr::null_mut(),
                COST_IO,
                BACKING_HEAP,
            )
        };
        // SAFETY: `handle`/`asset` are live and valid; `construct_streamed_chunk`
        // has the exact `ConstructFn` C-ABI signature.
        unsafe {
            deluge_resource_set_construct(handle, asset, Some(construct_streamed_chunk));
        }
        let payload_offset =
            deluge_sample_fill::chunk::deluge_streamed_chunk_payload_offset() as usize;
        let backing_size = payload_offset + CLUSTER_SIZE as usize + 7; // + kTrailingSlackBytes

        ChunkHarness {
            handle,
            asset,
            backing_size,
            _buf: buf,
        }
    }

    /// Requests chunk `index` (constructing it if needed), writes `make_ramp(index, CLUSTER_SIZE)`
    /// through the payload accessor, marks it ready and drops this call's lease.
    fn seed_and_mark_ready(&self, index: u32) {
        // SAFETY: `self.handle`/`self.asset` are live and valid.
        let ptr = unsafe { deluge_resource_try_acquire(self.handle, self.asset, index) };
        let ptr = if ptr.is_null() {
            // SAFETY: `self.handle`/`self.asset` are live; `self.backing_size` is what
            // `construct_streamed_chunk` expects.
            let p = unsafe {
                deluge_resource_request(self.handle, self.asset, index, self.backing_size)
            };
            assert!(!p.is_null(), "reserve for seeding index {index} failed");
            p
        } else {
            ptr
        };
        // SAFETY: `ptr` is a resident `StreamedChunk` backing; the accessor returns its
        // `CLUSTER_SIZE`-byte payload.
        let payload = unsafe { deluge_sample_fill::chunk::payload(ptr as *mut c_void) };
        let ramp = region_source_differential::make_ramp(index, CLUSTER_SIZE as usize);
        // SAFETY: `payload` is valid for `CLUSTER_SIZE` writable bytes; `ramp` is that long.
        unsafe { ptr::copy_nonoverlapping(ramp.as_ptr(), payload, CLUSTER_SIZE as usize) };
        // SAFETY: `self.handle`/`ptr` are live/valid per the calls above.
        unsafe { deluge_resource_mark_ready(self.handle, ptr) };
        // Drop this call's lease so it doesn't skew the tests' lease counts.
        // SAFETY: `self.handle`/`ptr` are live/valid.
        unsafe { deluge_resource_release(self.handle, ptr) };
    }

    /// Returns the total live hard-lease count across the chunk table, for lease-balance checks.
    fn total_leases(&self) -> u64 {
        (0..CHUNK_CAP)
            // SAFETY: `self.handle` is live; out-of-range slots report 0.
            .map(|slot| unsafe { deluge_resource_lease_count_by_slot(self.handle, slot) as u64 })
            .sum()
    }
}

/// A geometry whose last cluster holds only `SHORT_TAIL` bytes, exercising the short-last-cluster
/// clamp in `resident_bytes_for`.
const NUM_CLUSTERS: u32 = 6;
const SHORT_TAIL: u32 = 20;

fn geo() -> Geometry {
    Geometry {
        audio_data_start_bytes: 0,
        audio_data_length_bytes: (NUM_CLUSTERS as u64 - 1) * CLUSTER_SIZE as u64
            + SHORT_TAIL as u64,
        cluster_size_bytes: CLUSTER_SIZE,
        byte_depth: 2,
        num_channels: 1,
        raw_data_format: 0,
    }
}

/// Asserts a captured region's payload equals `make_ramp(index, resident_bytes)`. Header bytes
/// read through a backing-vs-payload mix-up will not match.
fn assert_payload_matches_ramp(out: &RegionOut, index: u32, label: &str) {
    // SAFETY: a ready region's `payload_base` is pinned for `resident_bytes` bytes while its lease
    // is held, which it is for this call.
    let bytes =
        unsafe { std::slice::from_raw_parts(out.payload_base, out.resident_bytes as usize) };
    let expected = region_source_differential::make_ramp(index, out.resident_bytes as usize);
    assert_eq!(
        bytes,
        expected.as_slice(),
        "{label}: payload bytes must equal make_ramp({index}) -- got the StreamedChunk header \
         (or some other wrong region) instead of the real payload"
    );
}

/// Drives a real `SampleSource<ManagerResidency>` through acquire, re-acquire, forward and reverse
/// walks, a state query, the short last cluster and retain/release/close; every ready payload must
/// match `make_ramp` and every lease must come back.
#[test]
fn cursor_resolves_real_streamed_chunk_payload_offset_correctly() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let h = ChunkHarness::new();
    for i in 0..NUM_CLUSTERS {
        h.seed_and_mark_ready(i);
    }
    assert_eq!(h.total_leases(), 0, "seeding must not leak any lease");

    // SAFETY: `h.handle` lives for the whole test.
    let residency =
        unsafe { ManagerResidency::new(h.handle, h.asset, CLUSTER_SIZE as usize, NUM_CLUSTERS) };
    let source = SampleSource::new(residency, geo());

    // Acquire forward.
    let (state0, out0) = source.acquire_ex(0, 1, 0);
    assert_eq!(state0, RegionState::Ready);
    let out0 = out0.expect("ready must fill out");
    assert_eq!(out0.region_index, 0);
    assert_eq!(
        out0.resident_bytes, CLUSTER_SIZE,
        "full cluster, not the last"
    );
    assert_payload_matches_ramp(&out0, 0, "acquire(0, forward)");

    // Re-acquire the same index: deduplicated, same payload and lease.
    let (state0b, out0b) = source.acquire_ex(0, 1, 0);
    assert_eq!(state0b, RegionState::Ready);
    let out0b = out0b.expect("ready must fill out");
    assert_eq!(
        out0b.payload_base, out0.payload_base,
        "re-acquiring the current index must dedupe to the identical payload pointer"
    );
    assert_eq!(
        out0b.lease, out0.lease,
        "dedupe must reuse the same lease token"
    );
    assert_payload_matches_ramp(&out0b, 0, "re-acquire(0)");

    // Forward to index 1, already prefetched by the ready acquire.
    let (state1, out1) = source.acquire_ex(1, 1, 0);
    assert_eq!(state1, RegionState::Ready);
    let out1 = out1.expect("ready must fill out");
    assert_eq!(out1.region_index, 1);
    assert_payload_matches_ramp(&out1, 1, "acquire(1, forward)");

    // Reverse back to index 0.
    let (state0c, out0c) = source.acquire_ex(0, -1, 0);
    assert_eq!(state0c, RegionState::Ready);
    let out0c = out0c.expect("ready must fill out");
    assert_eq!(out0c.region_index, 0);
    assert_payload_matches_ramp(&out0c, 0, "acquire(0, reverse)");

    // State query only.
    assert_eq!(source.state(0), RegionState::Ready);
    assert_eq!(
        source.state(NUM_CLUSTERS), // one past the end
        RegionState::Unavailable,
        "past the end of the stream"
    );

    // Short last cluster: `resident_bytes` clamps to the tail.
    let last = NUM_CLUSTERS - 1;
    let (state_last, out_last) = source.acquire_ex(last, 1, 0);
    assert_eq!(state_last, RegionState::Ready);
    let out_last = out_last.expect("ready must fill out");
    assert_eq!(out_last.region_index, last);
    assert_eq!(
        out_last.resident_bytes, SHORT_TAIL,
        "the last cluster's resident_bytes must clamp to the short tail"
    );
    assert_payload_matches_ramp(&out_last, last, "acquire(last, short cluster)");

    // Independent-pin retain/release balance.
    let baseline = h.total_leases();
    source.retain(out_last.lease);
    assert_eq!(
        h.total_leases(),
        baseline + 1,
        "retain must add exactly one lease"
    );
    source.release(out_last.lease);
    assert_eq!(h.total_leases(), baseline, "release must drop it back");

    // `close()` drops current, pending and prefetch; every lease must come back.
    source.close();
    assert_eq!(
        h.total_leases(),
        0,
        "close must release every lease the cursor held, back to the pre-acquire baseline"
    );
}

/// Non-vacuity: flipping one bit of a captured payload must make the ramp comparison fail.
#[test]
fn corrupted_payload_capture_diverges_from_the_ramp() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let h = ChunkHarness::new();
    h.seed_and_mark_ready(0);

    // SAFETY: `h.handle` is live for the whole test.
    let residency =
        unsafe { ManagerResidency::new(h.handle, h.asset, CLUSTER_SIZE as usize, NUM_CLUSTERS) };
    let source = SampleSource::new(residency, geo());

    let (state, out) = source.acquire_ex(0, 1, 0);
    assert_eq!(state, RegionState::Ready);
    let out = out.expect("ready must fill out");

    // SAFETY: same contract as `assert_payload_matches_ramp`.
    let captured =
        unsafe { std::slice::from_raw_parts(out.payload_base, out.resident_bytes as usize) }
            .to_vec();

    // The uncorrupted capture must match, so the failure below comes from the corruption.
    assert_eq!(
        captured,
        region_source_differential::make_ramp(0, captured.len()),
        "uncorrupted capture must match the ramp before this test corrupts a copy of it"
    );

    let mut corrupted = captured.clone();
    corrupted[3] ^= 0x01; // flip one bit
    assert_ne!(
        corrupted,
        region_source_differential::make_ramp(0, corrupted.len()),
        "a single-bit-corrupted capture must diverge from make_ramp -- if this ever \
         passes, the payload comparison this gate relies on has no teeth"
    );

    source.close();
}
