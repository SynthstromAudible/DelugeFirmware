//! The streamed sample-audio cluster's per-cluster state, backing the C++ `StreamedChunk`
//! type. A plain Rust struct (NOT repr(C) — nothing C++ reads it anymore; only its size feeds
//! the slab slot geometry). Drop-free: all fields are POD, so the manager still frees the slab slot
//! directly (no destructor to run), preserving the trivially-destructible chunk contract.
//!
//! Field set: the C++ `StreamedChunk` also carries `sample`/`resource_slot`,
//! but both are write-only in the live tree — their only C++ readers
//! (`StreamedChunk::convert_data_if_necessary`/`resource_lease_asset_id`/
//! `deluge::cluster::remove_reason(StreamedChunk&, ...)`, all in `cluster.cpp`) have zero call
//! sites anywhere (dead code, superseded by the async Rust fill + `convert.h`'s pure core), so
//! neither is carried here. `cluster_index` IS live: `deluge_sample_fill::fill_logic::begin`/
//! `finish` document and use it directly in offset math.

use core::ffi::c_void;

use crate::DelugeChunkConvertState;

/// Cache line, matching the C++ `CACHE_LINE_SIZE` (definitions.h). The front guard between the
/// struct header and the payload must be >= this; guarded C++-side by the slab reconcile.
const CACHE_LINE: usize = 32;

/// A resident chunk as the C ABI names it (`DelugeChunkBacking`, `include/libdeluge/types.h`).
///
/// Every non-null pointer of this type points at a [`StreamedChunk`], which only Rust dereferences.
#[repr(C)]
pub struct DelugeChunkBacking {
    _opaque: [u8; 0],
}

pub struct StreamedChunk {
    pub cluster_index: u32,
    pub loaded: bool,
    pub unloadable: bool,
    pub first_three_bytes: [u8; 3],
    pub extra_bytes_start_converted: bool,
    pub extra_bytes_end_converted: bool,
    pub payload: *mut u8,
}

impl StreamedChunk {
    const fn new(index: u32, payload: *mut u8) -> Self {
        StreamedChunk {
            cluster_index: index,
            loaded: false,
            unloadable: false,
            first_three_bytes: [0; 3],
            extra_bytes_start_converted: false,
            extra_bytes_end_converted: false,
            payload,
        }
    }
}

/// Byte offset of the payload from the slot base for a streamed chunk: header + one cache-line
/// front guard, matching the C++ `kChunkPayloadOffset` discipline (its own offset; the shared slab
/// slot takes the max with ComputedChunk's — see general_memory_allocator.cpp).
pub const RUST_CHUNK_PAYLOAD_OFFSET: usize = core::mem::size_of::<StreamedChunk>() + CACHE_LINE;

/// Placement-construct a StreamedChunk at `dest`, setting its payload base + cluster index.
/// # Safety
/// `dest` must be a writable slab slot of at least `RUST_CHUNK_PAYLOAD_OFFSET + Cluster::size +
/// trailing_guard` bytes.
pub(crate) unsafe fn construct(dest: *mut u8, index: u32) {
    // SAFETY: `dest` is a writable slab slot (caller contract); payload sits within it.
    unsafe {
        core::ptr::write(
            dest as *mut StreamedChunk,
            StreamedChunk::new(index, dest.add(RUST_CHUNK_PAYLOAD_OFFSET)),
        );
    }
}

/// C-ABI: the byte offset a streamed chunk needs for its payload, so the C++ slab setup can size
/// the shared slot as `max(this, ComputedChunk's kChunkPayloadOffset) + Cluster::size + guard`.
///
/// Unconditional `#[unsafe(no_mangle)]`, matching this crate's sibling export
/// (`deluge_streaming_set_fill_context` in `lib.rs`): unlike `deluge_sample_stream`/
/// `deluge_sample_source`, this crate declares no `host_app`/`sim` selector features (see
/// `Cargo.toml`'s `[features]`) — it is pulled in unconditionally wherever it's linked at all
/// (`native_fill` gates the manager-reaching fill, not symbol visibility), so there is no cfg to
/// gate this export behind.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streamed_chunk_payload_offset() -> u32 {
    RUST_CHUNK_PAYLOAD_OFFSET as u32
}

// ── Chunk construct + field accessors ─────────────────────────────────
// The streamed chunk's storage lives entirely in Rust: the resource manager placement-constructs it
// through `deluge_streaming_chunk_construct` (the sole remaining C-ABI export below, registered from
// `chunk_residency.cpp`), and every other caller — the native fill task, the C++ region cursor's Rust
// callers, the differential harnesses — reaches its fields only through the plain `pub fn` accessors
// below (`payload`/`set_loaded`/`loaded`/`unloadable`/`set_unloadable`/`convert_state`/
// `set_convert_state`). There is no longer a C++ caller for any of them; all access goes through
// these accessors directly by path (`deluge_sample_fill::chunk::*`).

/// C-ABI construct callback the resource manager invokes for a streamed SAMPLE chunk (registered from
/// `chunk_residency.cpp` via `deluge_resource_set_construct`). Matches the manager's
/// `DelugeResourceConstructFn` signature `(ctx, owner, index, dest)`. `ctx`/`owner` are unused: the
/// relocated chunk carries neither a context nor its owning `Sample*` — both were write-only in the
/// former C++ POD and dropped from the field set (see this module's header).
///
/// # Safety
/// `dest` is a manager-owned writable slab slot of at least `RUST_CHUNK_PAYLOAD_OFFSET + Cluster::size
/// + trailing guard` bytes (the manager's construct contract).
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_streaming_chunk_construct(
    _ctx: *mut c_void,
    _owner: *mut c_void,
    index: u32,
    dest: *mut c_void,
) {
    // SAFETY: `dest` is a manager-owned writable slab slot (construct contract above).
    unsafe { construct(dest as *mut u8, index) };
}

/// Reborrow an opaque chunk backing pointer as `&StreamedChunk`, for the read-only accessors
/// ([`payload`], [`loaded`], [`unloadable`], [`convert_state`]).
///
/// # Safety
/// `backing` must point at a live `StreamedChunk` this crate's [`construct`] placement-wrote into a
/// leased manager slab slot, handed back by the manager (`deluge_resource_loader_next`).
#[inline]
unsafe fn chunk_ref<'a>(backing: *mut c_void) -> &'a StreamedChunk {
    // SAFETY: caller contract — `backing` is a live constructed StreamedChunk.
    unsafe { &*(backing as *const StreamedChunk) }
}

/// Reborrow an opaque chunk backing pointer as `&mut StreamedChunk`, for the accessors that mutate a
/// field ([`set_loaded`], [`set_unloadable`], [`set_convert_state`]).
///
/// # Safety
/// `backing` must point at a live `StreamedChunk` this crate's [`construct`] placement-wrote into a
/// leased manager slab slot, handed back by the manager (`deluge_resource_loader_next`).
///
/// This crate does NOT synchronize the field this reborrow lets its caller write against concurrent
/// readers: `set_loaded` is written by the fill task while `loaded` (through [`chunk_ref`]) is
/// polled from the BSP drain — a genuine producer/consumer pair on different tasks, not "the single
/// fill task / cursor". The access is a plain, non-atomic read/write race by that description, but
/// it is the SAME race the prior C++ `StreamedChunk::loaded` field access already had (a bare `bool`
/// field, set by the fill context and polled by the cursor with no lock or atomic) — not a hazard
/// introduced by this relocation, just carried forward unchanged. Each write-accessor below holds
/// its `&mut` only for the duration of its own single field write, so no two writers alias it
/// concurrently; a concurrent reader observing a torn or stale value is the pre-existing behaviour.
#[inline]
unsafe fn chunk<'a>(backing: *mut c_void) -> &'a mut StreamedChunk {
    // SAFETY: caller contract — `backing` is a live, uniquely-borrowed constructed StreamedChunk.
    unsafe { &mut *(backing as *mut StreamedChunk) }
}

/// Base of the chunk's audio payload (the DMA/read destination and frame-read origin).
/// # Safety
/// See [`chunk_ref`]: `backing` is a live constructed StreamedChunk.
#[inline]
pub unsafe fn payload(backing: *mut c_void) -> *mut u8 {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk_ref()'s contract).
    unsafe { chunk_ref(backing).payload }
}

/// Mark the chunk's payload loaded/ready (the flag the C++ region cursor polls).
/// # Safety
/// See [`chunk`]: `backing` is a live constructed StreamedChunk.
#[inline]
pub unsafe fn set_loaded(backing: *mut c_void) {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk()'s contract).
    unsafe { chunk(backing).loaded = true };
}

/// Read the chunk's loaded/ready flag.
/// # Safety
/// See [`chunk_ref`]: `backing` is a live constructed StreamedChunk.
#[inline]
pub unsafe fn loaded(backing: *mut c_void) -> bool {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk_ref()'s contract).
    unsafe { chunk_ref(backing).loaded }
}

/// Read the chunk's unloadable flag.
/// # Safety
/// See [`chunk_ref`]: `backing` is a live constructed StreamedChunk.
#[inline]
pub unsafe fn unloadable(backing: *mut c_void) -> bool {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk_ref()'s contract).
    unsafe { chunk_ref(backing).unloadable }
}

/// Mark the chunk unloadable.
/// # Safety
/// See [`chunk`]: `backing` is a live constructed StreamedChunk.
#[inline]
pub unsafe fn set_unloadable(backing: *mut c_void) {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk()'s contract).
    unsafe { chunk(backing).unloadable = true };
}

/// Read the chunk's pre-conversion convert-state (first-three-bytes + boundary-stitch guards).
/// # Safety
/// See [`chunk_ref`]: `backing` is a live constructed StreamedChunk.
#[inline]
pub unsafe fn convert_state(backing: *mut c_void) -> DelugeChunkConvertState {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk_ref()'s contract).
    let c = unsafe { chunk_ref(backing) };
    DelugeChunkConvertState {
        first_three_bytes: c.first_three_bytes,
        start_converted: c.extra_bytes_start_converted,
        end_converted: c.extra_bytes_end_converted,
    }
}

/// Write the chunk's convert-state (the inverse of [`convert_state`]).
/// # Safety
/// See [`chunk`]: `backing` is a live constructed StreamedChunk.
#[inline]
pub unsafe fn set_convert_state(backing: *mut c_void, state: DelugeChunkConvertState) {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk()'s contract).
    let c = unsafe { chunk(backing) };
    c.first_three_bytes = state.first_three_bytes;
    c.extra_bytes_start_converted = state.start_converted;
    c.extra_bytes_end_converted = state.end_converted;
}

#[cfg(test)]
mod tests {
    // A plain `cargo test` on this `#![no_std]` crate links no allocator; `std` is available on
    // the host test binary, so pull it in here for `vec!` — mirrors `deluge_sample_stream`'s own
    // `#[cfg(test)] mod tests { extern crate std; ... }` convention (registry.rs).
    extern crate std;

    use std::vec;

    use super::*;

    #[test]
    fn payload_offset_clears_the_front_guard() {
        // The payload must sit at least one cache line past the struct header, so the front
        // guard (offset - struct size) covers the DMA cache-maintenance range-rounding + the
        // application front-underread — exactly the C++ kChunkPayloadOffset discipline.
        assert!(RUST_CHUNK_PAYLOAD_OFFSET >= core::mem::size_of::<StreamedChunk>() + CACHE_LINE);
        assert_eq!(
            RUST_CHUNK_PAYLOAD_OFFSET - core::mem::size_of::<StreamedChunk>(),
            CACHE_LINE
        );
    }

    #[test]
    fn construct_sets_payload_index_and_defaults() {
        // A 4KB-aligned-enough backing buffer big enough for the header+guard.
        let mut buf = vec![0u8; RUST_CHUNK_PAYLOAD_OFFSET + 64];
        let dest = buf.as_mut_ptr();
        // SAFETY: `dest` owns RUST_CHUNK_PAYLOAD_OFFSET+64 writable bytes for this test.
        unsafe { construct(dest, 7) };
        // SAFETY: construct placement-wrote a StreamedChunk at dest.
        let c = unsafe { &*(dest as *const StreamedChunk) };
        assert_eq!(c.cluster_index, 7);
        assert!(!c.loaded);
        assert!(!c.unloadable);
        assert_eq!(c.payload, unsafe { dest.add(RUST_CHUNK_PAYLOAD_OFFSET) });
    }

    // Construct a chunk into a fresh backing buffer and hand back the opaque backing pointer the
    // C-ABI accessors take.
    fn constructed(buf: &mut std::vec::Vec<u8>, index: u32) -> *mut c_void {
        let dest = buf.as_mut_ptr();
        // SAFETY: `buf` owns RUST_CHUNK_PAYLOAD_OFFSET+64 writable bytes (allocated by the caller).
        unsafe { construct(dest, index) };
        dest as *mut c_void
    }

    #[test]
    fn construct_c_abi_matches_direct_construct() {
        let mut buf = vec![0u8; RUST_CHUNK_PAYLOAD_OFFSET + 64];
        let dest = buf.as_mut_ptr();
        // SAFETY: `buf` owns the slot bytes; ctx/owner are unused by the callback.
        unsafe {
            deluge_streaming_chunk_construct(
                core::ptr::null_mut(),
                core::ptr::null_mut(),
                11,
                dest as *mut c_void,
            );
        }
        // SAFETY: the callback placement-wrote a StreamedChunk at dest.
        let c = unsafe { &*(dest as *const StreamedChunk) };
        assert_eq!(c.cluster_index, 11);
        assert_eq!(c.payload, unsafe { dest.add(RUST_CHUNK_PAYLOAD_OFFSET) });
        // SAFETY: dest is a live constructed chunk.
        assert_eq!(unsafe { payload(dest as *mut c_void) }, unsafe {
            dest.add(RUST_CHUNK_PAYLOAD_OFFSET)
        });
    }

    #[test]
    fn loaded_flag_round_trips() {
        let mut buf = vec![0u8; RUST_CHUNK_PAYLOAD_OFFSET + 64];
        let backing = constructed(&mut buf, 0);
        // SAFETY: backing is a live constructed chunk for the duration of this test.
        unsafe {
            assert!(!loaded(backing));
            set_loaded(backing);
            assert!(loaded(backing));
        }
    }

    #[test]
    fn unloadable_flag_round_trips() {
        let mut buf = vec![0u8; RUST_CHUNK_PAYLOAD_OFFSET + 64];
        let backing = constructed(&mut buf, 0);
        // SAFETY: backing is a live constructed chunk for the duration of this test.
        unsafe {
            assert!(!unloadable(backing));
            set_unloadable(backing);
            assert!(unloadable(backing));
        }
    }

    #[test]
    fn convert_state_round_trips() {
        let mut buf = vec![0u8; RUST_CHUNK_PAYLOAD_OFFSET + 64];
        let backing = constructed(&mut buf, 0);
        let written = DelugeChunkConvertState {
            first_three_bytes: [0xDE, 0xAD, 0xBE],
            start_converted: true,
            end_converted: false,
        };
        // SAFETY: backing is a live constructed chunk for the duration of this test.
        let read = unsafe {
            set_convert_state(backing, written);
            convert_state(backing)
        };
        assert_eq!(read.first_three_bytes, written.first_three_bytes);
        assert_eq!(read.start_converted, written.start_converted);
        assert_eq!(read.end_converted, written.end_converted);
    }
}
