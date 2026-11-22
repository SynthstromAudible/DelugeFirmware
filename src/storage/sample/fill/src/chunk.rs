//! A streamed sample cluster's state, living at the front of its resource-manager slab slot.
//!
//! [`StreamedChunk`] is Rust-only (not `#[repr(C)]`); C++ sees only its payload offset, which
//! feeds the slab slot geometry. Every field is plain data with no destructor, so the manager can
//! free the slot directly. The resource manager constructs chunks through
//! [`deluge_streaming_chunk_construct`]; everything else reaches the fields through the `pub`
//! accessors here.

use core::ffi::c_void;

use crate::DelugeChunkConvertState;

/// Cache line, matching the C++ `CACHE_LINE_SIZE`. The front guard between the header and the
/// payload must be at least this.
const CACHE_LINE: usize = 32;

/// A resident chunk as the C ABI names it (`DelugeChunkBacking` in `libdeluge/types.h`).
///
/// Every non-null pointer of this type points at a [`StreamedChunk`], which only Rust dereferences.
#[repr(C)]
pub struct DelugeChunkBacking {
    _opaque: [u8; 0],
}

/// One streamed cluster's state; see the module docs.
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

/// Byte offset of a streamed chunk's payload from its slot base: the header plus a one-cache-line
/// front guard. The shared slab slot uses the larger of this and the C++ `kChunkPayloadOffset`.
pub const RUST_CHUNK_PAYLOAD_OFFSET: usize = core::mem::size_of::<StreamedChunk>() + CACHE_LINE;

/// Placement-constructs a [`StreamedChunk`] for cluster `index` at `dest`.
///
/// # Safety
///
/// `dest` must be a writable slab slot of at least `RUST_CHUNK_PAYLOAD_OFFSET + Cluster::size +
/// trailing guard` bytes.
pub(crate) unsafe fn construct(dest: *mut u8, index: u32) {
    // SAFETY: `dest` is a writable slab slot (caller contract) and the payload lies within it.
    unsafe {
        core::ptr::write(
            dest as *mut StreamedChunk,
            StreamedChunk::new(index, dest.add(RUST_CHUNK_PAYLOAD_OFFSET)),
        );
    }
}

/// Returns [`RUST_CHUNK_PAYLOAD_OFFSET`], for the C++ slab setup to size the shared slot.
///
/// Exported unconditionally, like this crate's other exports: it has no `c_abi` feature.
#[unsafe(no_mangle)]
pub extern "C" fn deluge_streamed_chunk_payload_offset() -> u32 {
    RUST_CHUNK_PAYLOAD_OFFSET as u32
}

// Construction and field accessors.

/// The resource manager's construct callback for a streamed sample chunk, a
/// `DelugeResourceConstructFn` registered from `chunk_residency.cpp`. `_ctx` and `_owner` are
/// unused.
///
/// # Safety
///
/// `dest` must be a manager-owned writable slab slot of at least
/// `RUST_CHUNK_PAYLOAD_OFFSET + Cluster::size + trailing guard` bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn deluge_streaming_chunk_construct(
    _ctx: *mut c_void,
    _owner: *mut c_void,
    index: u32,
    dest: *mut c_void,
) {
    // SAFETY: `dest` is a manager-owned writable slab slot (caller contract).
    unsafe { construct(dest as *mut u8, index) };
}

/// Reborrows a chunk backing pointer as `&StreamedChunk`, for the read-only accessors.
///
/// # Safety
///
/// `backing` must point at a live chunk that [`construct`] wrote into a leased manager slab slot.
#[inline]
unsafe fn chunk_ref<'a>(backing: *mut c_void) -> &'a StreamedChunk {
    // SAFETY: `backing` is a live constructed chunk (caller contract).
    unsafe { &*(backing as *const StreamedChunk) }
}

/// Reborrows a chunk backing pointer as `&mut StreamedChunk`, for the mutating accessors.
///
/// # Safety
///
/// As for [`chunk_ref`]. The fields are not synchronized: each accessor holds the `&mut` for a
/// single field write only, and callers must not read a field another context may be writing.
#[inline]
unsafe fn chunk<'a>(backing: *mut c_void) -> &'a mut StreamedChunk {
    // SAFETY: `backing` is a live constructed chunk, not otherwise borrowed (caller contract).
    unsafe { &mut *(backing as *mut StreamedChunk) }
}

/// Returns the base of the chunk's audio payload: the read destination and frame-read origin.
///
/// # Safety
///
/// `backing` must point at a live [`StreamedChunk`] constructed in a leased manager slab slot.
#[inline]
pub unsafe fn payload(backing: *mut c_void) -> *mut u8 {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk_ref()'s contract).
    unsafe { chunk_ref(backing).payload }
}

/// Marks the chunk's payload loaded.
///
/// # Safety
///
/// `backing` must point at a live [`StreamedChunk`] constructed in a leased manager slab slot.
/// The write is not synchronized: no other context may access the field meanwhile.
#[inline]
pub unsafe fn set_loaded(backing: *mut c_void) {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk()'s contract).
    unsafe { chunk(backing).loaded = true };
}

/// Returns whether the chunk's payload is loaded.
///
/// # Safety
///
/// `backing` must point at a live [`StreamedChunk`] constructed in a leased manager slab slot.
#[inline]
pub unsafe fn loaded(backing: *mut c_void) -> bool {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk_ref()'s contract).
    unsafe { chunk_ref(backing).loaded }
}

/// Returns whether the chunk is marked unloadable.
///
/// # Safety
///
/// `backing` must point at a live [`StreamedChunk`] constructed in a leased manager slab slot.
#[inline]
pub unsafe fn unloadable(backing: *mut c_void) -> bool {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk_ref()'s contract).
    unsafe { chunk_ref(backing).unloadable }
}

/// Marks the chunk unloadable.
///
/// # Safety
///
/// `backing` must point at a live [`StreamedChunk`] constructed in a leased manager slab slot.
/// The write is not synchronized: no other context may access the field meanwhile.
#[inline]
pub unsafe fn set_unloadable(backing: *mut c_void) {
    // SAFETY: `backing` is a live constructed StreamedChunk (chunk()'s contract).
    unsafe { chunk(backing).unloadable = true };
}

/// Returns the chunk's convert state.
///
/// # Safety
///
/// `backing` must point at a live [`StreamedChunk`] constructed in a leased manager slab slot.
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

/// Sets the chunk's convert state.
///
/// # Safety
///
/// `backing` must point at a live [`StreamedChunk`] constructed in a leased manager slab slot.
/// The write is not synchronized: no other context may access the field meanwhile.
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
    // The crate is `no_std`; the host test binary has `std`, needed here for `vec!`.
    extern crate std;

    use std::vec;

    use super::*;

    #[test]
    fn payload_offset_clears_the_front_guard() {
        // The front guard must be a full cache line, covering DMA cache-maintenance rounding and
        // the reader's front under-read, as for the C++ `kChunkPayloadOffset`.
        assert!(RUST_CHUNK_PAYLOAD_OFFSET >= core::mem::size_of::<StreamedChunk>() + CACHE_LINE);
        assert_eq!(
            RUST_CHUNK_PAYLOAD_OFFSET - core::mem::size_of::<StreamedChunk>(),
            CACHE_LINE
        );
    }

    #[test]
    fn construct_sets_payload_index_and_defaults() {
        // Big enough for the header and guard.
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

    // Constructs a chunk into `buf` and returns the backing pointer the accessors take.
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
