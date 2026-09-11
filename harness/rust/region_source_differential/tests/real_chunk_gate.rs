//! A payload-offset gate: drives the Rust region-port cursor
//! (`deluge_sample_source::cursor::SampleSource<ManagerResidency>`) over a REAL
//! `deluge_resource` manager whose chunk backing is a REAL streamed chunk — not
//! the Vec-backed fake that the retired region_differential harness's own
//! `RustBackend`/`HarnessResidency` seeded (`rust_backend.rs`), which stored each cluster's
//! "payload" directly in a bare `Vec<u8>` with NO header in front of it, so `payload == backing`
//! there by construction. That made `ManagerPin::payload()`'s pre-fix bug — returning the
//! raw manager backing pointer (the chunk HEADER) instead of routing through
//! `deluge_sample_fill::chunk::payload` (`backing + payload_offset`, the real
//! PAYLOAD) — invisible to every existing gate. This test proves the fix
//! against the geometry that actually exercises it.
//!
//! ## How the real streamed-chunk backing is built
//!
//! The streamed chunk's storage (construct + all seven field accessors) lives
//! in `deluge_sample_fill::chunk` (Rust); this gate drives the real Rust
//! construct/accessors directly, no C++ at all. [`construct_streamed_chunk`] (a thin signature-matching
//! wrapper — see its own doc) placement-constructs a real `StreamedChunk` at a
//! `deluge_resource` asset's slab-slot base, with its payload pointer set to
//! `base + payload_offset` (`ChunkHarness::new`, mirroring
//! `native_finish_glue.rs::ChunkHarness::new`). `ChunkHarness::seed_and_mark_ready`
//! then writes `make_ramp(index)` through the REAL `deluge_sample_fill::chunk::payload`
//! accessor (the same real, compiler-computed offset `ManagerPin::payload()` itself
//! calls through) — never a raw `backing.add(b)` write, which would land in the
//! header instead of the payload. The manager backing pointer (`try_acquire`'s
//! return, offset 0) and the payload pointer (`deluge_sample_fill::chunk::payload`'s
//! return, offset `payload_offset`, always non-zero — see
//! `deluge_sample_fill::chunk`'s own doc) are therefore GENUINELY DIFFERENT
//! addresses, exactly the geometry a backing-vs-payload confusion needs to be
//! byte-detectable: the header bytes `StreamedChunk`'s own fields hold (a payload
//! pointer, cluster_index, a few bools) don't coincidentally equal `make_ramp`'s
//! pattern, so a cursor reading from the wrong pointer fails the ramp comparison
//! immediately.
//!
//! ## How the cursor is driven
//!
//! Directly via `deluge_sample_source`'s own Rust API —
//! `ManagerResidency::new(handle, asset, cluster_size, num_clusters)` +
//! `SampleSource::new(residency, geo)` — the SAME construction `rust_backend.rs`'s
//! `RustBackend::open` and `manager_residency.rs`'s/`cursor.rs`'s own unit tests
//! use, NOT the `deluge_sample_source_open` C-ABI bridge (`abi.rs`).
//! Both `ManagerResidency` and `SampleSource` are already `pub` — no visibility
//! change was needed. The bridge would additionally require test doubles for
//! `deluge_streaming_resource_manager`/`deluge_sample_stream_asset_id` (the
//! `open()` seam) and the allocation-free source pool's slot-claim machinery,
//! none of which this gate's payload-offset question needs — the direct API is
//! the minimal composition that drives the real cursor over a real chunk without
//! dragging in that extra layer.
//!
//! ## Teeth
//!
//! Verified by hand (never committed): reverting `ManagerPin::payload()`
//! (`manager_residency.rs`) to `self.lease.chunk().as_ptr() as *const u8` (the
//! pre-fix body) makes `cursor_resolves_real_streamed_chunk_payload_offset_correctly`
//! fail immediately — the captured bytes are the `StreamedChunk` header, not
//! `make_ramp`. Restored immediately after (`git diff` on that file empty
//! again) — no bug is committed.
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

// `deluge_resource::sync::Masked` (used by both `deluge_resource` and
// `deluge_sample_source::cursor`) calls out to three `extern "C"`
// critical-section primitives. This crate links both as plain non-test rlibs (no
// `--cfg test` on THEIR compilations, only ours), so their own `#[cfg(test)]`
// stubs are compiled out of what this binary pulls in — this test binary must
// supply the three symbols itself, or the link fails with `undefined symbol:
// ENTER_CRITICAL_SECTION` (etc). Identical pattern to the retired region_differential harness's
// `tests::differential::host_critical_section_stubs` and
// `deluge_sample_source::host_critical_section_stubs`. `deluge_in_interrupt` is
// hard-wired `false` — this gate never models the audio-ISR context — so masking
// is genuinely exercised, not bypassed.
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
    // `deluge_resource` asks two more things of its host since 2026-08-16: whether it is standing on the
    // audio render context (to skip its critical section there -- never true here, so it always masks), and
    // the audio sample timer for its loader-latency stats (instrumentation only; nothing here reads them).
    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_audio_context() -> bool {
        false
    }
    #[unsafe(no_mangle)]
    extern "C" fn deluge_debug_now_frames() -> u32 {
        0
    }
}

/// `ManagerResidency::acquire`'s Loading branch (`manager_residency.rs`) calls this
/// after `loader_enqueue` — a real, app-provided symbol in production
/// (`sample_stream.cpp`'s `get_cluster`), absent here since this test never spawns
/// a real loader/fill task and drives every index straight to `Ready` via
/// `ChunkHarness::seed_and_mark_ready` before the cursor ever sees it (mirrors
/// the retired region_differential harness's `rust_backend::HarnessResidency`, which likewise
/// never simulated a fill landing mid-run). A Loading acquire can still fire during
/// this gate's own op sequence (e.g. an out-of-range/not-yet-seeded index, or a
/// prefetch neighbour this test didn't pre-seed) and must not stall/panic — a
/// no-op stand-in is correct here, same tier as the critical-section stubs above.
#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_signal_fill() {}

/// Test double for `deluge_sample_stream_asset_id` (`abi.rs`'s `open()` bridge,
/// unused by this gate — see the module doc's "How the cursor is driven"). Only
/// present as a defensive link-time stand-in in case some codegen-unit grouping
/// pulls `abi.rs`'s compiled code in alongside the `manager_residency`/`cursor`
/// code this gate actually exercises; never called by anything this test does.
#[unsafe(no_mangle)]
extern "C" fn deluge_sample_stream_asset_id(_stream_backing: *mut c_void) -> u32 {
    0
}

/// Placement-construct callback matching `deluge_resource::ConstructFn`'s exact
/// signature (`dest: *mut u8`), forwarding to the real
/// `deluge_sample_fill::chunk::deluge_streaming_chunk_construct` — the SAME
/// callback the resource manager invokes for a real streamed SAMPLE chunk in
/// production (`chunk_residency.cpp`). A thin wrapper is needed only because
/// that function's own `dest` parameter is typed `*mut c_void` (its C-ABI
/// declared shape) while `ConstructFn` requires `*mut u8` — the two are
/// ABI-identical (both a plain data pointer), just declared with different
/// pointee types on each side of the boundary, so Rust's function-pointer
/// typing (unlike C's) won't let one satisfy the other directly. Mirrors
/// `region_fill_differential::tests::native_finish_glue::construct_streamed_chunk`.
unsafe extern "C" fn construct_streamed_chunk(
    ctx: *mut c_void,
    owner: *mut c_void,
    index: u32,
    dest: *mut u8,
) {
    // SAFETY: forwards `dest` unchanged (only its declared pointee type differs) to
    // the real construct function, under the same manager construct-contract this
    // function's own caller (`deluge_resource_request`, via
    // `deluge_resource_set_construct`) upholds.
    unsafe {
        deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
            ctx,
            owner,
            index,
            dest as *mut c_void,
        );
    }
}

/// `cargo test` runs `#[test]`s in parallel threads; `deluge_resource::sync::Masked`
/// and the critical-section stubs above are process-global state, so any two
/// tests building a manager at once would race it. Every test takes this lock for
/// its whole run — same pattern as the retired region_differential harness's and
/// `deluge_sample_source`'s own `TEST_LOCK`s.
static TEST_LOCK: Mutex<()> = Mutex::new(());

const CLUSTER_SIZE: u32 = 64;
const CHUNK_CAP: u32 = 32; // generous headroom over this gate's handful of indices

/// A real `deluge_resource` manager over a throwaway test heap, with one
/// requestable asset whose `construct` places a REAL streamed chunk (payload at
/// `backing + payload_offset`) at each chunk's slab-slot base. Mirrors
/// `native_finish_glue.rs::ChunkHarness`, minus the fill-orchestration pieces
/// this gate doesn't need (no active-manager plumbing —
/// `deluge_streaming_resource_manager` isn't in this gate's call path — the
/// cursor is driven directly over `handle`/`asset`, not through that lookup).
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
        // SAFETY: `buf` is a live, 16-aligned, `words * 16`-byte arena for as long
        // as `self` (and thus `buf`) is alive.
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
        // The Rust-owned streamed-chunk payload offset — the real,
        // compiler-computed value reported over its own C-ABI, not hand-derived.
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

    /// Reserve (constructing a REAL `StreamedChunk` if not already resident),
    /// write `make_ramp(index, CLUSTER_SIZE)` through the REAL
    /// `deluge_sample_fill::chunk::payload` accessor (never a raw backing-pointer
    /// write — see the module doc), mark it ready, and release the reservation
    /// lease this call itself took. Mirrors `manager_residency.rs`'s/`cursor.rs`'s
    /// own `mark_index_ready` test helper, except the seed write goes through the
    /// real payload accessor over a real `StreamedChunk`, not a raw
    /// `dest.add(b)` write into pre-construct backing bytes.
    fn seed_and_mark_ready(&self, index: u32) {
        // SAFETY: `self.handle`/`self.asset` are live and valid.
        let ptr = unsafe { deluge_resource_try_acquire(self.handle, self.asset, index) };
        let ptr = if ptr.is_null() {
            // SAFETY: `self.handle`/`self.asset` are live/valid; `self.backing_size`
            // is exactly what `construct_streamed_chunk` (via
            // `deluge_streamed_chunk_payload_offset`) expects.
            let p = unsafe {
                deluge_resource_request(self.handle, self.asset, index, self.backing_size)
            };
            assert!(!p.is_null(), "reserve for seeding index {index} failed");
            p
        } else {
            ptr
        };
        // SAFETY: `ptr` is a live, resident `StreamedChunk*` backing from the call
        // above; `deluge_sample_fill::chunk::payload` returns `ptr + payload_offset`,
        // `CLUSTER_SIZE` bytes of which are this chunk's own payload allocation.
        let payload = unsafe { deluge_sample_fill::chunk::payload(ptr as *mut c_void) };
        let ramp = region_source_differential::make_ramp(index, CLUSTER_SIZE as usize);
        // SAFETY: `payload` is non-null and valid for `CLUSTER_SIZE` writable bytes
        // per the call above; `ramp` holds exactly that many bytes.
        unsafe { ptr::copy_nonoverlapping(ramp.as_ptr(), payload, CLUSTER_SIZE as usize) };
        // SAFETY: `self.handle`/`ptr` are live/valid per the calls above.
        unsafe { deluge_resource_mark_ready(self.handle, ptr) };
        // The lease taken by `try_acquire`/`request` above is ours to drop — release
        // it so it doesn't skew a test's own lease-count assertions.
        // SAFETY: `self.handle`/`ptr` are live/valid.
        unsafe { deluge_resource_release(self.handle, ptr) };
    }

    /// Sum of every slot's live hard-lease count across the manager's whole chunk
    /// table — the lease-balance oracle, matching `cursor.rs`'s/
    /// `manager_residency.rs`'s own `total_leases` test helper.
    fn total_leases(&self) -> u64 {
        (0..CHUNK_CAP)
            // SAFETY: `self.handle` is live; `lease_count_by_slot` tolerates any
            // slot index (returns 0 out of range), per its own doc.
            .map(|slot| unsafe { deluge_resource_lease_count_by_slot(self.handle, slot) as u64 })
            .sum()
    }
}

/// `geo`'s last cluster (index `NUM_CLUSTERS - 1`) has a short tail: `SHORT_TAIL`
/// bytes instead of a full `CLUSTER_SIZE`, so `resident_bytes_for`'s short-last-
/// cluster clamp is genuinely exercised (mirrors
/// the retired region_differential harness's `cpp_backend::Scenario::dense` short-tail choice).
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

/// Captured region's payload bytes must equal `make_ramp(index, resident_bytes)` —
/// the offset-correctness assertion this whole gate exists to make: if
/// `ManagerPin::payload()` ever again returns the `StreamedChunk` HEADER (the
/// pre-fix bug — see the module doc's "Teeth" section) instead of the payload
/// (`backing + payload_offset`), the header's own field bytes (a pointer, an
/// index, a couple of bools) will not coincidentally match this pattern and this
/// assertion fails immediately.
fn assert_payload_matches_ramp(out: &RegionOut, index: u32, label: &str) {
    // SAFETY: `out.payload_base` is a live, `out.resident_bytes`-byte region per
    // `acquire_ex`'s own READY contract — pinned resident for as long as the lease
    // this `RegionOut` carries is held, which is true for the duration of this call.
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

/// The gate itself: an `Op` sequence (acquire forward, re-acquire, forward walk,
/// reverse, state, short-last-cluster, retain/release/close) driven through a
/// real `SampleSource<ManagerResidency>` over the real-`StreamedChunk` harness
/// above — reusing the retired region_differential harness's op vocabulary in spirit; there is no
/// second backend to diff against here, only the `make_ramp` oracle each `Ready` acquire's payload
/// must match.
#[test]
fn cursor_resolves_real_streamed_chunk_payload_offset_correctly() {
    let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    let h = ChunkHarness::new();
    for i in 0..NUM_CLUSTERS {
        h.seed_and_mark_ready(i);
    }
    assert_eq!(h.total_leases(), 0, "seeding must not leak any lease");

    // SAFETY: `h.handle` is live for the whole test (leaked-for-test-life arena).
    let residency =
        unsafe { ManagerResidency::new(h.handle, h.asset, CLUSTER_SIZE as usize, NUM_CLUSTERS) };
    let source = SampleSource::new(residency, geo());

    // -- Acquire forward. --
    let (state0, out0) = source.acquire_ex(0, 1, 0);
    assert_eq!(state0, RegionState::Ready);
    let out0 = out0.expect("ready must fill out");
    assert_eq!(out0.region_index, 0);
    assert_eq!(
        out0.resident_bytes, CLUSTER_SIZE,
        "full cluster, not the last"
    );
    assert_payload_matches_ramp(&out0, 0, "acquire(0, forward)");

    // -- Re-acquire the same index: idempotent dedupe, same payload/lease. --
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

    // -- Forward walk to index 1 (the prior READY acquire already prefetched it). --
    let (state1, out1) = source.acquire_ex(1, 1, 0);
    assert_eq!(state1, RegionState::Ready);
    let out1 = out1.expect("ready must fill out");
    assert_eq!(out1.region_index, 1);
    assert_payload_matches_ramp(&out1, 1, "acquire(1, forward)");

    // -- Reverse back to index 0. --
    let (state0c, out0c) = source.acquire_ex(0, -1, 0);
    assert_eq!(state0c, RegionState::Ready);
    let out0c = out0c.expect("ready must fill out");
    assert_eq!(out0c.region_index, 0);
    assert_payload_matches_ramp(&out0c, 0, "acquire(0, reverse)");

    // -- Pure state query, no acquire. --
    assert_eq!(source.state(0), RegionState::Ready);
    assert_eq!(
        source.state(NUM_CLUSTERS), // one past the end
        RegionState::Unavailable,
        "past the end of the stream"
    );

    // -- Short last cluster: resident_bytes clamps to the tail, and the ramp
    // comparison is over exactly that many (short) bytes. --
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

    // -- Independent-pin retain/release balance. --
    let baseline = h.total_leases();
    source.retain(out_last.lease);
    assert_eq!(
        h.total_leases(),
        baseline + 1,
        "retain must add exactly one lease"
    );
    source.release(out_last.lease);
    assert_eq!(h.total_leases(), baseline, "release must drop it back");

    // -- close() drops current + pending + prefetch; every lease this cursor ever
    // took (including any standing prefetch from the walk above) must come back. --
    source.close();
    assert_eq!(
        h.total_leases(),
        0,
        "close must release every lease the cursor held, back to the pre-acquire baseline"
    );
}

/// Non-vacuity: mirrors the retired region_differential harness's `perturb` "corrupt one captured
/// byte, assert the comparison detects it" pattern. Proves the ramp comparison
/// `assert_payload_matches_ramp` performs above actually has teeth — a captured
/// payload that has been tampered with must NOT equal `make_ramp` any more; if
/// this test ever passed with the corruption left in place, the whole gate above
/// would be checking nothing.
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

    // Sanity: the UNCORRUPTED capture matches — so the divergence asserted below is
    // caused by the corruption, not a pre-existing mismatch this test would trip on
    // regardless.
    assert_eq!(
        captured,
        region_source_differential::make_ramp(0, captured.len()),
        "uncorrupted capture must match the ramp before this test corrupts a copy of it"
    );

    let mut corrupted = captured.clone();
    corrupted[3] ^= 0x01; // flip one bit — mirrors Perturbation::FlipPayloadByte
    assert_ne!(
        corrupted,
        region_source_differential::make_ramp(0, corrupted.len()),
        "a single-bit-corrupted capture must diverge from make_ramp -- if this ever \
         passes, the payload comparison this gate relies on has no teeth"
    );

    source.close();
}
