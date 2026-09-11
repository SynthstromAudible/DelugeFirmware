//! Cross-path/single-store regression: every OTHER host gate exercises either
//! `fill_logic::finish_convert_stitch` in isolation (over hand-built plain buffers — `differential.rs`,
//! `tests/fill_logic_host.rs`) or `streaming_loader::fill_once`/`FillOps` against a fully FAKE
//! `FillOps` (`tests/streaming_fill_host.rs`, in `deluge-bsp-rust`), or `ProdOps::begin`/`finish`'s
//! ORCHESTRATION only, with plain local `ConvertState` values standing in for the real store
//! (`tests/host_end_to_end.rs`, this crate). None of them drive
//! `deluge_sample_fill::native_finish` itself — its `try_acquire` → payload-accessor →
//! convert-state-accessor → stitch → write-back chain — over the REAL streamed-chunk accessors.
//! That gap is exactly where review found TWO real, silent-corruption bugs:
//!
//!  1. **The convert-state double-store**: before unification, the synchronous C++ fill path
//!     (`read_cluster_data` → `finish_fill`) wrote convert-state directly
//!     onto the chunk, while the async Rust fill task wrote a SEPARATE Rust-side sidecar table — two
//!     disjoint stores. A boundary stitch between a sync-loaded cluster and an async-prefetched
//!     neighbour read the EMPTY store, corrupting the boundary silently on every affected note.
//!     Structurally fixed by routing BOTH paths through `native_finish` over the ONE chunk accessor
//!     store (`deluge_streaming_chunk_convert_state`/`_set_convert_state`) — this test proves that
//!     unification actually holds by driving what were the "two paths" (two separate `finish` calls,
//!     mirroring a sync-loaded neighbour and an async-loaded self) through the SAME real store and
//!     confirming the second call reads the first's write-back, not a zeroed default.
//!  2. **Backing-vs-payload**: `native_finish` built each neighbour's payload slice
//!     directly from the raw `try_acquire` pointer (the chunk's BACKING) instead of
//!     `deluge_streaming_chunk_payload(p)` (`backing + kChunkPayloadOffset`, the chunk's PAYLOAD) — the
//!     neighbour stitch read/wrote the neighbour's header bytes instead of its samples.
//!
//! ## How this test drives the REAL glue
//!
//! `native_finish`/`native_begin` live in the shared `deluge_sample_fill` crate, `pub`
//! and behind that crate's `native_fill` feature (this crate's `Cargo.toml` turns it on as a
//! dev-dependency). So this test calls `deluge_sample_fill::native_begin`/`native_finish` directly —
//! no more `#[path]`-recompiling `deluge-bsp-rust`'s `streaming_loader.rs`/`fill_logic.rs`, no more
//! `ProdOps`/`FillOps` indirection. Driving `deluge_sample_fill::native_finish(chunk, true)` directly
//! IS driving the real function — the same one `deluge-bsp-rust`'s `ProdOps::finish` (a one-line call
//! straight into it) and the range reader's synchronous `fill_now` (`deluge_sample_reader`) both call.
//!
//! `deluge_sample_fill::native`'s `unsafe extern "C"` block (gated `native_fill`) declares 9 symbols
//! this test must supply. Four (`deluge_resource_chunk_ident`, `_try_acquire`, `_release`,
//! `_mark_ready`) are real `#[no_mangle]` Rust symbols from the `deluge_resource` crate (already a
//! dev-dependency) — genuinely real, no test double. The streamed chunk's storage
//! (construct + the seven field accessors) lives entirely in this same `deluge_sample_fill`
//! crate (`chunk.rs`) — so the four remaining accessors (`chunk::payload`/`set_loaded`/
//! `convert_state`/`set_convert_state`, plain `pub fn`s with no `#[no_mangle]` C-ABI wrappers) are
//! now ALSO real, no-test-double calls, satisfied by `deluge_sample_fill`'s own
//! object code (this crate already depends on it) — redefining the same four symbols here would be a
//! link-time duplicate against `deluge_sample_fill`'s own copies, not a meaningful test double.
//!
//! The ONE symbol this test still has to provide by hand is `deluge_streaming_resource_manager`
//! (below) — NOT part of either Critical's bug surface (both bugs were in `native_finish`'s own
//! neighbour-payload/convert-state wiring, not in "which manager is the global one"); real
//! production code resolves it via `GeneralMemoryAllocator::get().resourceManager()`, which this
//! host test doesn't compile, so it stands in with a plain `AtomicPtr` set by [`ChunkHarness::new`] —
//! the same test-local tier `tests/host_end_to_end.rs` already uses for its own
//! `ENTER_CRITICAL_SECTION`/`EXIT_CRITICAL_SECTION`/`deluge_in_interrupt`.
//!
//! Chunk construction goes straight through `deluge_sample_fill::chunk::deluge_streaming_chunk_construct`
//! too (via [`construct_streamed_chunk`], a thin signature-matching wrapper — see its own doc for why
//! it needs to exist at all) — the SAME callback the resource manager invokes for a real streamed
//! SAMPLE chunk in production (`chunk_residency.cpp`), not a hand-rolled stand-in.
//!
//! ## What this test does NOT exercise
//!
//! - `native_begin` (the sector/byte-offset descriptor arithmetic) — untouched by either Critical
//!   (both were `finish`-side), already covered byte-identically by `fill_logic_host.rs`'s `begin`
//!   tests and `differential.rs`. This test seeds each chunk's payload directly, mirroring "the read
//!   already completed".
//! - `Sample`/`SampleStream`, or `GeneralMemoryAllocator` — this test never touches the app's
//!   storage/model closure; only the relocated Rust chunk + the shared resource manager.
//! - The efatfs read (`ProdOps::read`, in `deluge-bsp-rust`) — this test never calls it; covered
//!   elsewhere (`fs_differential`, `streaming_fill_host.rs`'s descriptor-plumbing test).
//! - On-device timing/DMA — this is a host, single-threaded, synchronous exercise of the same calls
//!   the async task and the sync fill path make; the real hardware validation is Kate's on-device gate
//!   (non-native-format playback across note-on/seek/loop — see the task brief).
//!
//! ## Teeth: this test was verified (by hand, outside the committed suite) to FAIL on both Criticals
//!
//! Both original bugs were reintroduced locally against this exact test (one at a time, `git
//! checkout`-restored immediately after) and confirmed to fail. The mechanism each perturbation breaks:
//! - **Backing-vs-payload**: [`CLUSTER_SIZE`] is chosen so `CLUSTER_SIZE + 7` lands STRICTLY between
//!   `size_of::<StreamedChunk>()` and the payload offset (see [`geometry_and_layout_invariants_hold`]) —
//!   a neighbour-payload read/write from the raw backing pointer instead of the payload accessor would
//!   read/corrupt the struct's own fields and the unnamed front-guard padding, never reaching the real
//!   seeded payload ramp at all, so the stitched output diverges from the C++ reference immediately.
//! - **Double-store**: the single-visit boundaries alone (prev/next both fresh, each visited exactly
//!   once by self) turned out NOT to have teeth for this class by themselves — `stitch_next`/
//!   `stitch_prev`'s "already converted?" branch (`stitch.cpp`) is false on every first visit
//!   regardless of which store answered it, so a neighbour-state read landing on a zeroed default
//!   happens to coincide with the correct "never touched" value there (verified empirically: the
//!   naive version of this test, checking only the single-visit boundaries plus a bare post-hoc
//!   convert-state readback, still PASSED with the double-store bug reintroduced). The part of
//!   `cross_path_finish_matches_cpp_reference_over_real_streamed_chunks` that actually has teeth is
//!   its second act: PREV gets overwritten with fresh bytes ("reloaded") and re-`finish`ed, now with
//!   SELF as its ready NEXT — since self's start_converted is TRUE by then (set by self's own
//!   `finish` above), prev's reload must read that true value back through the SAME store to
//!   correctly take the "already converted" branch (which stages the recompute from self's captured
//!   `unconverted_head`, not its current payload) — a disjoint/empty second store answers false
//!   instead, taking the wrong branch and producing concretely different bytes in prev's reloaded
//!   payload. [`neighbour_convert_state_is_read_back_through_the_shared_store`] complements this by
//!   proving the WRITE side (the flags really do land in the shared store, non-default), but the
//!   reload re-`finish` is what proves the READ side actually depends on it.
#![cfg(not(target_os = "none"))]

use core::ffi::c_void;
use core::ptr;
use core::sync::atomic::{AtomicPtr, Ordering};

use deluge_resource::{
    BACKING_HEAP, DelugeResource, deluge_resource_create, deluge_resource_define_asset,
    deluge_resource_request, deluge_resource_set_construct,
};
use deluge_sample_fill::FillContext;
use deluge_sample_fill::chunk::StreamedChunk;

// Single-threaded-per-test critical section: `deluge_resource`'s `sync::Masked` calls these three
// C-ABI symbols; this test never models the audio-ISR context, so inert no-op stand-ins are
// semantically correct (nothing here can race with itself — a single thread, no concurrent "ISR").
// Same stand-ins `tests/host_end_to_end.rs` already uses.
#[unsafe(no_mangle)]
extern "C" fn ENTER_CRITICAL_SECTION() {}
#[unsafe(no_mangle)]
extern "C" fn EXIT_CRITICAL_SECTION() {}
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

/// The one process-wide resource manager pointer `deluge_streaming_resource_manager` (just below)
/// hands back to `deluge_sample_fill::native_begin`/`native_finish` — see the module doc's "How this
/// test drives the REAL glue" section for why this is the one piece of plumbing this test still
/// provides by hand.
static ACTIVE_MANAGER: AtomicPtr<c_void> = AtomicPtr::new(ptr::null_mut());

#[unsafe(no_mangle)]
extern "C" fn deluge_streaming_resource_manager() -> *mut c_void {
    ACTIVE_MANAGER.load(Ordering::Relaxed)
}

/// Placement-construct callback matching `deluge_resource::ConstructFn`'s exact signature
/// (`dest: *mut u8`), forwarding to the real `deluge_sample_fill::chunk::deluge_streaming_chunk_construct`
/// — the SAME callback the resource manager invokes for a real streamed SAMPLE chunk in production
/// (`chunk_residency.cpp`). A thin wrapper is needed only because that function's own `dest` parameter
/// is typed `*mut c_void` (its C-ABI declared shape) while `ConstructFn` requires `*mut u8` — the two
/// are ABI-identical (both a plain data pointer), just declared with different pointee types on each
/// side of the boundary, so Rust's function-pointer typing (unlike C's) won't let one satisfy the
/// other directly.
unsafe extern "C" fn construct_streamed_chunk(
    ctx: *mut c_void,
    owner: *mut c_void,
    index: u32,
    dest: *mut u8,
) {
    // SAFETY: forwards `dest` unchanged (only its declared pointee type differs) to the real
    // construct function, under the same manager construct-contract this function's own caller
    // (`deluge_resource_request`, via `deluge_resource_set_construct`) upholds.
    unsafe {
        deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
            ctx,
            owner,
            index,
            dest as *mut c_void,
        );
    }
}

/// Small cluster (32 bytes) chosen so `CLUSTER_SIZE + 7` (a neighbour's full payload-plus-trailing-slack
/// span, 39 bytes) lands strictly between `size_of::<StreamedChunk>()` (24 bytes) and the chunk's
/// payload offset (56 bytes, `RUST_CHUNK_PAYLOAD_OFFSET` — queried at runtime via
/// [`ChunkHarness::payload_offset`], not hard-coded here) — see
/// [`geometry_and_layout_invariants_hold`] and the module doc's "Teeth" section.
const CLUSTER_SIZE: u32 = 32;
const CLUSTER_MAGNITUDE: u32 = 5; // 2^5 = 32

/// Misaligned start (mirrors `fill_logic`'s own `stitch_geo()`), UNSIGNED_8 (non-native — the
/// corruption class the task brief calls out), audio data long enough that none of clusters 0/1/2 is
/// the last.
fn geo() -> FillContext {
    FillContext {
        efatfs_handle: 0, // unused: this test never calls native_begin / the read step
        audio_data_start_pos_bytes: 1,
        audio_data_length_bytes: 100_000,
        first_cluster_index_with_no_audio_data: 10,
        cluster_size: CLUSTER_SIZE,
        cluster_size_magnitude: CLUSTER_MAGNITUDE,
        raw_data_format: 2, // Unsigned8
        byte_depth: 1,      // matches raw_data_format: Unsigned8
        num_channels: 1,
    }
}

fn cpp_geo() -> region_fill_differential::cpp_ref::Geometry {
    let g = geo();
    region_fill_differential::cpp_ref::Geometry {
        audio_data_start_pos_bytes: g.audio_data_start_pos_bytes,
        audio_data_length_bytes: g.audio_data_length_bytes,
        first_cluster_index_with_no_audio_data: g.first_cluster_index_with_no_audio_data,
        cluster_size: g.cluster_size as usize,
        cluster_size_magnitude: g.cluster_size_magnitude as usize,
        raw_data_format: g.raw_data_format,
    }
}

/// A manager over a throwaway test heap with one requestable, real-`StreamedChunk`-backed test asset —
/// mirrors `tests/host_end_to_end.rs::TestManager`, except `construct` placement-news a REAL
/// `StreamedChunk` ([`construct_streamed_chunk`]) instead of stamping raw bytes.
struct ChunkHarness {
    handle: *mut DelugeResource,
    asset: u32,
    payload_offset: usize,
    header_size: usize,
    backing_size: usize,
    _buf: Vec<u128>,
}

impl ChunkHarness {
    fn new() -> Self {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf = vec![0u128; words];
        // SAFETY: `buf` is a live, 16-aligned, `words * 16`-byte arena for as long as `self` (and
        // thus `buf`) is alive.
        let heap = unsafe { fs_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        // SAFETY: `heap` is the live handle just created above.
        let handle = unsafe { deluge_resource_create(heap, 4, 8) };
        assert!(!handle.is_null());
        // SAFETY: `handle` is live for the duration of this call.
        let asset = unsafe {
            deluge_resource_define_asset(
                handle,
                ptr::null_mut(),
                None,
                None,
                ptr::null_mut(),
                1,
                BACKING_HEAP,
            )
        };
        // SAFETY: `handle`/`asset` are live and valid; `construct_streamed_chunk` has the exact
        // `ConstructFn` C-ABI signature.
        unsafe {
            deluge_resource_set_construct(handle, asset, Some(construct_streamed_chunk));
        }
        // Pure queries, no preconditions: the payload offset is Rust-owned and reported over
        // its own C-ABI; the header size is this crate's own reflection of the (pub) Rust struct's
        // real, compiler-computed size — not a hand-derived guess.
        let payload_offset =
            deluge_sample_fill::chunk::deluge_streamed_chunk_payload_offset() as usize;
        let header_size = core::mem::size_of::<StreamedChunk>();
        let backing_size = payload_offset + CLUSTER_SIZE as usize + 7;
        // SAFETY: `handle` is the live manager just created; stored for `deluge_streaming_resource_manager`
        // to hand back — must happen before any `native_begin`/`native_finish` call reads it.
        ACTIVE_MANAGER.store(handle as *mut c_void, Ordering::Relaxed);

        // Register this asset's fill-context — `native_finish`'s `resolve()` looks this up via
        // `deluge_sample_fill::fill_context_for(asset)`; without it every `finish` call fails
        // closed (`resolve` returns `None`) before ever reaching the neighbour gather this test is
        // here to exercise.
        deluge_sample_fill::deluge_streaming_set_fill_context(ptr::null_mut(), asset, geo());

        ChunkHarness {
            handle,
            asset,
            payload_offset,
            header_size,
            backing_size,
            _buf: buf,
        }
    }

    /// Reserve + construct chunk `index` (async/`request` path — no I/O), returning its raw backing
    /// pointer (`== StreamedChunk*`, offset 0 — NOT the payload; see [`Self::payload_of`]).
    fn request(&self, index: u32) -> *mut u8 {
        // SAFETY: `self.handle`/`self.asset` are live and valid; `self.backing_size` is exactly what
        // `construct_streamed_chunk` (via `deluge_streamed_chunk_payload_offset`) expects.
        unsafe { deluge_resource_request(self.handle, self.asset, index, self.backing_size) }
    }

    /// The chunk's payload base (`backing + payload_offset`), as a byte slice covering its full
    /// `CLUSTER_SIZE + 7`-byte trailing-slack span.
    ///
    /// # Safety
    /// `backing` must be a live, exclusively-held chunk backing from [`Self::request`], and the
    /// returned slice's lifetime must not outlive that exclusivity.
    unsafe fn payload_of<'a>(&self, backing: *mut u8) -> &'a mut [u8] {
        // SAFETY: `backing` is a resident chunk this harness constructed with `payload_offset` bytes
        // of front guard and `CLUSTER_SIZE + 7` bytes of real payload after it (see `backing_size`'s
        // construction); forwarded to the caller's own exclusivity contract.
        unsafe {
            core::slice::from_raw_parts_mut(
                backing.add(self.payload_offset),
                CLUSTER_SIZE as usize + 7,
            )
        }
    }

    /// The unnamed front-guard padding `[header_size, payload_offset)` — never a real `StreamedChunk`
    /// field, so a correct `finish` must never touch it. See the module doc's "Teeth" section.
    ///
    /// # Safety
    /// Same contract as [`Self::payload_of`].
    unsafe fn front_guard_of<'a>(&self, backing: *mut u8) -> &'a mut [u8] {
        let len = self.payload_offset - self.header_size;
        // SAFETY: forwarded to the caller; `backing + header_size .. backing + payload_offset` is
        // entirely within this chunk's own backing allocation (`backing_size` bytes).
        unsafe { core::slice::from_raw_parts_mut(backing.add(self.header_size), len) }
    }
}

/// One neighbour/self chunk's request + seeded payload + captured front-guard snapshot, threading
/// everything a test needs to drive `native_finish` and later assert on it.
struct SeededChunk {
    backing: *mut u8,
    front_guard_before: Vec<u8>,
}

fn seed(h: &ChunkHarness, index: u32, tag: u32) -> SeededChunk {
    let backing = h.request(index);
    assert!(!backing.is_null(), "request failed for index {index}");
    // SAFETY: `backing` was just resident-constructed by `request`, exclusively held here (nothing
    // else touches it until `native_finish` is called on it below).
    let payload = unsafe { h.payload_of(backing) };
    payload.copy_from_slice(&region_fill_differential::ramp(
        tag,
        CLUSTER_SIZE as usize + 7,
    ));
    // Stamp the front guard with a distinct, recognizable marker BEFORE any `finish` call, so a
    // later re-read can prove it was never touched (see the module doc's "Teeth" section).
    // SAFETY: same as above.
    let guard = unsafe { h.front_guard_of(backing) };
    guard.fill(0xC0u8.wrapping_add(tag as u8));
    let front_guard_before = guard.to_vec();
    SeededChunk {
        backing,
        front_guard_before,
    }
}

/// Full cross-path scenario: three real `StreamedChunk`-backed clusters (prev=0, self=1, next=2)
/// over one real `deluge_resource` manager. `native_finish` is called on prev, then next, then self —
/// by the time self's `finish` runs, BOTH neighbours are already resident+ready (their own `finish`
/// already published them via `deluge_resource_mark_ready`), so self's `native_finish` genuinely
/// exercises `try_acquire` → the payload accessor → the convert-state accessor → stitch →
/// write-back for both edges — the real cross-path/single-store scenario (mirrors a sync-loaded
/// neighbour, then an async-loaded self, both funnelled through the ONE native tail).
///
/// Asserts byte-identity against the SAME C++ reference (`cpp_ref::finish_fill_over_buffers`) the
/// fill-differential (`differential.rs`) uses, run in the identical 3-call order over independently
/// seeded plain buffers — proving this isn't just "the wiring runs without crashing" but that the
/// REAL glue produces the exact same bytes/flags an independently-implemented C++ orchestration
/// does, for a NON-NATIVE format (the corruption class both Criticals hit).
#[test]
fn cross_path_finish_matches_cpp_reference_over_real_streamed_chunks() {
    let h = ChunkHarness::new();
    let prev = seed(&h, 0, 1000);
    let self_c = seed(&h, 1, 1);
    let next = seed(&h, 2, 2000);

    // -- Real cross-path drive: prev, then next (each with no ready neighbour yet), then self
    // (both neighbours now resident+ready). --
    assert!(deluge_sample_fill::native_finish(
        prev.backing as *mut c_void,
        true
    ));
    assert!(deluge_sample_fill::native_finish(
        next.backing as *mut c_void,
        true
    ));
    assert!(deluge_sample_fill::native_finish(
        self_c.backing as *mut c_void,
        true
    ));

    // -- Read back the real StreamedChunk state after the full sequence. --
    // SAFETY: all three backings are still resident (never released/evicted).
    let self_payload_after = unsafe { h.payload_of(self_c.backing) }.to_vec();
    let prev_payload_after = unsafe { h.payload_of(prev.backing) }.to_vec();
    let next_payload_after = unsafe { h.payload_of(next.backing) }.to_vec();

    // -- Independent C++ reference, run in the SAME 3-call order over independently seeded plain
    // buffers (same ramp tags as `seed` above). --
    let mut prev_buf = region_fill_differential::ramp(1000, CLUSTER_SIZE as usize + 7);
    let mut self_buf = region_fill_differential::ramp(1, CLUSTER_SIZE as usize + 7);
    let mut next_buf = region_fill_differential::ramp(2000, CLUSTER_SIZE as usize + 7);
    let g = cpp_geo();

    let mut prev_state = region_fill_differential::cpp_ref::ConvertState::default();
    let mut next_state = region_fill_differential::cpp_ref::ConvertState::default();
    let mut self_state = region_fill_differential::cpp_ref::ConvertState::default();

    // prev's own finish: no prev-of-prev, no next (self not "ready" yet in this replay either).
    region_fill_differential::cpp_ref::finish_fill_over_buffers(
        &mut prev_buf,
        0,
        &g,
        &mut prev_state,
        None,
        None,
    );
    // next's own finish: no prev (self not ready), no next-of-next.
    region_fill_differential::cpp_ref::finish_fill_over_buffers(
        &mut next_buf,
        2,
        &g,
        &mut next_state,
        None,
        None,
    );
    // self's own finish: both neighbours now "ready" — their post-finish buffers/flags feed in.
    let (mut prev_start_unused, mut prev_end) = (false, prev_state.end_converted);
    let (mut next_start, mut next_end_unused) = (next_state.start_converted, false);
    region_fill_differential::cpp_ref::finish_fill_over_buffers(
        &mut self_buf,
        1,
        &g,
        &mut self_state,
        Some(region_fill_differential::cpp_ref::Neighbour {
            payload: &mut prev_buf,
            unconverted_head: &prev_state.first_three_bytes, // unread on the prev side
            start_converted: &mut prev_start_unused,
            end_converted: &mut prev_end,
        }),
        Some(region_fill_differential::cpp_ref::Neighbour {
            payload: &mut next_buf,
            unconverted_head: &next_state.first_three_bytes,
            start_converted: &mut next_start,
            end_converted: &mut next_end_unused,
        }),
    );

    assert_eq!(
        self_payload_after, self_buf,
        "self payload diverged from the C++ reference — the cross-path glue produced different \
         bytes than an independently-implemented orchestration over the SAME inputs/order"
    );
    assert_eq!(
        prev_payload_after, prev_buf,
        "prev payload diverged (self's finish stitches prev's OWN tail bytes)"
    );
    assert_eq!(
        next_payload_after, next_buf,
        "next payload diverged (self's finish stitches next's OWN head bytes)"
    );

    // Sanity: a real boundary conversion actually happened on both edges (misaligned, non-native) —
    // otherwise this whole scenario would vacuously pass with an all-native/no-op stitch.
    assert!(
        self_state.start_converted && self_state.end_converted,
        "expected a real straddle conversion on both self edges — geometry/format sanity check"
    );

    // -- Independent readback through the REAL accessor, bypassing native_finish entirely — proves
    // the write-back landed in the ONE shared StreamedChunk store, not a zeroed default or a
    // different store than what a later reader (or the manager's own next fill) would see. --
    neighbour_convert_state_is_read_back_through_the_shared_store(&prev, &next);

    // -- Cross-path/single-store proof, part 2: PREV gets evicted and reloaded (fresh raw bytes,
    // as if freshly read off disk again) and re-`finish`ed, now with SELF as its ready NEXT
    // neighbour. Self's own finish above already wrote `self.start_converted = true` into the
    // shared store as a side effect (see the `self_state.start_converted` assertion above) — prev's
    // reload MUST see that true value (not a zeroed default) to correctly take
    // `stitch_next`'s "already converted" branch (`stitch.cpp`), which stages the straddle
    // recompute from SELF's OWN CAPTURED PRE-CONVERSION bytes (`unconverted_head`) rather than
    // self's current, already-converted payload — a concretely different, byte-detectable
    // outcome if that neighbour-state read fell back to a disjoint/empty second store (the
    // double-store bug's exact failure mode). The single-visit boundaries proved above (prev/next
    // both fresh, visited exactly once each) can't exercise this branch at all — `already_converted`
    // is false either way on a first visit — so this second act is the part of the scenario that
    // actually distinguishes "shared store" from "double store" for the READ side, complementing
    // `neighbour_convert_state_is_read_back_through_the_shared_store`'s proof for the WRITE side.
    {
        let prev_reload_tag = 3000;
        // SAFETY: `prev.backing` is still resident (never released/evicted) — this overwrite
        // models a fresh disk read into the same still-leased slot, exactly as a real reload
        // would (the manager's own eviction/reload cycle is out of scope here; this test drives
        // `native_finish` directly, mirroring what a reload's post-read call would do).
        unsafe { h.payload_of(prev.backing) }.copy_from_slice(&region_fill_differential::ramp(
            prev_reload_tag,
            CLUSTER_SIZE as usize + 7,
        ));
        assert!(deluge_sample_fill::native_finish(
            prev.backing as *mut c_void,
            true
        ));
        // SAFETY: `prev.backing` is still resident.
        let prev_reloaded_after = unsafe { h.payload_of(prev.backing) }.to_vec();

        let mut prev_buf2 =
            region_fill_differential::ramp(prev_reload_tag, CLUSTER_SIZE as usize + 7);
        let mut prev_state2 = region_fill_differential::cpp_ref::ConvertState::default();
        // `self_buf`/`self_state` here are exactly what the THIRD reference call above already
        // computed and verified byte-identical to the real self chunk — self's role as prev's
        // "next" reads its OWN start_converted (true) and unconverted_head (its captured
        // pre-conversion bytes); its end_converted is unused in the "next" role (see
        // `cpp_ref::Neighbour`'s field docs), and self's payload is only READ here, never mutated
        // (the "already converted" stitch path never writes back into `next.head`).
        let mut self_start_for_reload = self_state.start_converted;
        let mut self_end_unused = false;
        region_fill_differential::cpp_ref::finish_fill_over_buffers(
            &mut prev_buf2,
            0,
            &g,
            &mut prev_state2,
            None,
            Some(region_fill_differential::cpp_ref::Neighbour {
                payload: &mut self_buf,
                unconverted_head: &self_state.first_three_bytes,
                start_converted: &mut self_start_for_reload,
                end_converted: &mut self_end_unused,
            }),
        );

        assert_eq!(
            prev_reloaded_after, prev_buf2,
            "prev's RELOAD payload diverged from the C++ reference — the second finish's read of \
             self's shared convert-state (start_converted/unconverted_head) didn't match what \
             self's own finish actually published, the double-store bug's exact symptom"
        );
    }

    // -- Front-guard non-corruption: the unnamed padding between the struct's real fields and its
    // payload must be byte-identical to what `seed` stamped — proves the stitch touched the PAYLOAD
    // region, never the header/front-guard. See the module doc's "Teeth" section. --
    for (name, c) in [("prev", &prev), ("self", &self_c), ("next", &next)] {
        // SAFETY: all three backings are still resident.
        let guard_after = unsafe { h.front_guard_of(c.backing) };
        assert_eq!(
            guard_after, c.front_guard_before,
            "{name}'s front-guard padding was touched by finish — a backing-vs-payload confusion \
             would corrupt exactly this region"
        );
    }
}

/// Independent proof that prev/next's convert-state, as read back through the REAL
/// `deluge_sample_fill::chunk::convert_state` accessor AFTER self's `finish` wrote it back, matches what
/// self's `finish` actually computed — NOT the zeroed default `ConvertState::default()` a
/// disjoint/empty second store (the pre-unification double-store bug's observable symptom) would
/// report. Split out as its own function (rather than inlined above) so its intent reads as a
/// standalone claim, matching the task brief's own framing ("confirm native_finish(N) reads it back
/// (not a zeroed default)").
fn neighbour_convert_state_is_read_back_through_the_shared_store(
    prev: &SeededChunk,
    next: &SeededChunk,
) {
    // SAFETY: `prev.backing`/`next.backing` are still resident, still valid, live constructed
    // `StreamedChunk`s (this crate's own [`ChunkHarness`] never releases or evicts them).
    let prev_state_now =
        unsafe { deluge_sample_fill::chunk::convert_state(prev.backing as *mut c_void) };
    // SAFETY: same as above.
    let next_state_now =
        unsafe { deluge_sample_fill::chunk::convert_state(next.backing as *mut c_void) };

    // self's own edges being converted (asserted above) implies BOTH neighbours' shared boundary
    // flags were flipped true by self's write-back — the exact bit a disjoint-store bug would leave
    // at its zeroed default (false) forever, no matter how many times it's re-read.
    assert!(
        prev_state_now.end_converted,
        "prev's end_converted must read back TRUE through the shared store after self's finish \
         wrote it back — a zeroed default here means the write-back went to a different store than \
         this readback (the double-store bug's exact symptom)"
    );
    assert!(
        next_state_now.start_converted,
        "next's start_converted must read back TRUE through the shared store after self's finish \
         wrote it back — same double-store symptom as prev's end_converted above"
    );
}

/// Sanity/self-check on this test's own layout assumptions (see the module doc's "Teeth" section):
/// `CLUSTER_SIZE + 7` (a neighbour's full payload span) must land strictly between
/// `size_of::<StreamedChunk>()` and the chunk's payload offset — otherwise a hypothetical
/// backing-vs-payload bug's wrong `[0, CLUSTER_SIZE+7)` span could either (a) stay entirely within
/// the struct's own named fields without ever reaching the recognizable front-guard marker, or (b)
/// reach all the way into the REAL payload region, muddying whether a failure came from the right
/// cause. If this ever fails (e.g. `StreamedChunk` grows a field), `CLUSTER_SIZE` needs raising to
/// match — a loud build/test failure here, not silently losing this test's teeth.
#[test]
fn geometry_and_layout_invariants_hold() {
    let header_size = core::mem::size_of::<StreamedChunk>();
    let payload_offset = deluge_sample_fill::chunk::deluge_streamed_chunk_payload_offset() as usize;
    let wrong_span_len = CLUSTER_SIZE as usize + 7;
    assert!(
        wrong_span_len > header_size,
        "CLUSTER_SIZE + 7 ({wrong_span_len}) must exceed size_of::<StreamedChunk>() \
         ({header_size}) so a backing-vs-payload bug's wrong span reaches the front-guard marker"
    );
    assert!(
        wrong_span_len <= payload_offset,
        "CLUSTER_SIZE + 7 ({wrong_span_len}) must not exceed the payload offset ({payload_offset}) \
         so a backing-vs-payload bug's wrong span never reaches the REAL payload region"
    );
}
