//! [`Reader`], a frame cursor over one sample's source residency, plus the stateless [`peek`] and
//! [`invalidate`].
//!
//! `open` resolves geometry; `window`/`advance` do the reads: a cache-hit acquire, a fill requested
//! from the async drain on a miss, and a single self-pinned lease. Close is ordinary `Drop`.

use core::ffi::c_void;

use deluge_resource::facade::Lease;
use deluge_resource::{DelugeResource, Resource};
use deluge_sample_fill::chunk::DelugeChunkBacking;

/// Mirrors `DelugeReadHint` (`libdeluge/sample_reader.h`). `repr(u8)` matches the header's
/// `: uint8_t`, as it crosses the FFI by value.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ReadHint {
    /// Share the source residency cache: clusters the voice already holds are free hits.
    Cached = 0,
    /// One-shot whole-sample scan (waveform pre-scan, wavetable build) that should not displace the
    /// voice's working set. Currently has no effect (see `Reader::acquire_and_fill`).
    Scan = 1,
}

/// Mirrors `DelugeSampleGeometry` (`libdeluge/sample_source.h`) field for field.
///
/// A local copy rather than a dependency on `deluge_sample_source`, keeping the non-voice reader
/// decoupled from the voice cursor.
#[repr(C)]
#[derive(Clone, Copy, Default, Debug)]
pub struct Geometry {
    pub audio_data_start_bytes: u32,
    pub audio_data_length_bytes: u64,
    pub cluster_size_bytes: u32,
    pub byte_depth: u8,
    pub num_channels: u8,
    pub raw_data_format: u8,
}

/// The "still recording, length unknown" value of `audio_data_length_bytes`
/// (`sample_recorder.cpp`). Mirrored by hand, like [`Geometry`].
pub(crate) const UNKNOWN_LENGTH_SENTINEL: u64 = 0x8FFF_FFFF_FFFF_FFFF;

/// Returns the valid payload bytes of cluster `index`: the full cluster size, the remainder for a
/// short last cluster, or `0` past the end. A zero or unknown length (recording, or not yet
/// finalized) counts every cluster as full. Mirrors
/// `deluge_sample_source::geometry::resident_bytes_for`.
fn resident_bytes_for(index: u32, geo: &Geometry) -> u32 {
    let cluster_size = geo.cluster_size_bytes;
    if geo.audio_data_length_bytes == 0 || geo.audio_data_length_bytes == UNKNOWN_LENGTH_SENTINEL {
        return cluster_size;
    }
    let audio_data_end = geo.audio_data_length_bytes + geo.audio_data_start_bytes as u64;
    let start_this_cluster = index as u64 * cluster_size as u64;
    if audio_data_end <= start_this_cluster {
        return 0;
    }
    let bytes_to_read = audio_data_end - start_this_cluster;
    if bytes_to_read < cluster_size as u64 {
        bytes_to_read as u32
    } else {
        cluster_size
    }
}

/// Maps a registered [`deluge_sample_fill::FillContext`] onto [`Geometry`]. Shared by
/// [`Reader::open`] and [`peek`] so both resolve geometry identically.
fn geometry_from_context(ctx: deluge_sample_fill::FillContext) -> Geometry {
    Geometry {
        audio_data_start_bytes: ctx.audio_data_start_pos_bytes,
        audio_data_length_bytes: ctx.audio_data_length_bytes,
        cluster_size_bytes: ctx.cluster_size,
        byte_depth: ctx.byte_depth,
        num_channels: ctx.num_channels,
        raw_data_format: ctx.raw_data_format,
    }
}

/// Maps `frame` to `(cluster_index, byte_offset, resident_bytes)`.
///
/// Mirrors `sample.cpp`'s mapping (`sourceBytePos = audioDataStartPosBytes + frame *
/// bytesPerSample`, then shift and mask by `Cluster::size`); `cluster_size_bytes` is always a power
/// of two, so `/` and `%` are equivalent. Returns `None` only for malformed geometry (zero frame
/// stride or cluster size); a position past the end resolves with `resident_bytes == 0`, the
/// caller's EOF signal.
fn locate(frame: u64, geo: &Geometry) -> Option<(u32, u32, u32)> {
    let frame_stride = geo.byte_depth as u64 * geo.num_channels as u64;
    if frame_stride == 0 || geo.cluster_size_bytes == 0 {
        return None;
    }
    let abs_byte_pos =
        (geo.audio_data_start_bytes as u64).saturating_add(frame.saturating_mul(frame_stride));
    let cluster_size = geo.cluster_size_bytes as u64;
    let cluster_index = (abs_byte_pos / cluster_size).min(u32::MAX as u64) as u32;
    let byte_offset = (abs_byte_pos % cluster_size) as u32;
    let resident = resident_bytes_for(cluster_index, geo);
    Some((cluster_index, byte_offset, resident))
}

unsafe extern "C" {
    /// Returns the process-wide resource manager (`libdeluge/sample_stream.h`), which lives for the
    /// rest of the process once non-null.
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// Enqueues a reserved chunk's fill on the async drain and returns at once
    /// (`libdeluge/streaming_fill.h`; the BSP's `storage::streaming_loader`). The queue holds its
    /// own lease, so the fill survives the reader dropping its lease.
    pub fn deluge_streaming_request_chunk(chunk_backing: *mut DelugeChunkBacking);
}

/// A frame cursor over one sample's source residency: the state behind `DelugeSampleReader`.
pub struct Reader {
    asset: u32,
    geometry: Geometry,
    direction: i8,
    hint: ReadHint,
    current_frame: u64,
    /// Whether the most recent `window()` succeeded. Not sticky: a transient card error or OOM is
    /// retried by the next `window()`. EOF leaves it `true`. A reader over an asset with no
    /// registered fill-context has zeroed geometry, so every `window()` fails.
    ok: bool,
    /// The resource manager, resolved once at `open()`.
    manager: *mut c_void,
    // `pub(crate)` so `abi.rs`'s tests can attach a lease directly.
    pub(crate) held_lease: Option<Lease>,
    /// The cluster `held_lease` pins. `advance()` drops the lease when the cursor leaves it.
    held_cluster: Option<u32>,
}

impl Reader {
    /// Creates a reader over `asset` (the header's `source_id`: the asset
    /// `deluge_streaming_define_asset` defined for this sample, shared with the voice port), at
    /// `start_frame`, reading in `direction` (+1 forward, -1 reverse).
    ///
    /// Geometry comes from the asset's registered fill-context
    /// ([`deluge_sample_fill::fill_context_for`]), not the caller. Without one, geometry stays
    /// zeroed and the reader is not ok. No I/O and no lease.
    pub fn open(asset: u32, start_frame: u64, direction: i8, hint: ReadHint) -> Self {
        // SAFETY: no preconditions; returns the process-wide manager or null.
        let manager = unsafe { deluge_streaming_resource_manager() };
        let (geometry, ok) = match deluge_sample_fill::fill_context_for(asset) {
            Some(ctx) => (geometry_from_context(ctx), true),
            None => (Geometry::default(), false),
        };
        Reader {
            asset,
            geometry,
            direction,
            hint,
            current_frame: start_frame,
            ok,
            manager,
            held_lease: None,
            held_cluster: None,
        }
    }

    /// Releases any held pin and moves the cursor to `frame`; the next `window()` re-pins.
    pub fn seek(&mut self, frame: u64) {
        self.held_lease = None; // Releases the lease, if any.
        self.held_cluster = None;
        self.current_frame = frame;
    }

    pub fn asset(&self) -> u32 {
        self.asset
    }

    pub fn geometry(&self) -> Geometry {
        self.geometry
    }

    pub fn direction(&self) -> i8 {
        self.direction
    }

    pub fn hint(&self) -> ReadHint {
        self.hint
    }

    pub fn current_frame(&self) -> u64 {
        self.current_frame
    }

    /// Returns whether the most recent `window()` succeeded (see the `ok` field).
    pub fn ok(&self) -> bool {
        self.ok
    }

    /// Returns a lease on cluster `cluster_index` once it is ready.
    ///
    /// A cache hit (`acquire_leased`) returns at once. A miss `request`s a fresh reservation and
    /// hands its fill to the async drain; if the drain has not finished when that call returns,
    /// this returns `None` and the caller retries on its next tick, which hits. Also `None` on a
    /// reservation failure. Nothing leaks either way.
    ///
    /// [`ReadHint::Scan`] has no effect yet: no manager call this crate can reach takes a
    /// per-acquire eviction or recency hint (eviction rank depends only on the asset's cost, size
    /// and recency), so the hint is stored but unused.
    fn acquire_and_fill(&self, cluster_index: u32) -> Option<Lease> {
        if self.manager.is_null() {
            // No manager yet (boot ordering).
            return None;
        }
        // SAFETY: `self.manager` is non-null and lives for the rest of the process.
        let resource = unsafe { Resource::from_handle(self.manager as *mut DelugeResource) };
        if let Some(lease) = resource.acquire_leased(self.asset, cluster_index) {
            return Some(lease);
        }
        let lease = resource.request(
            self.asset,
            cluster_index,
            self.geometry.cluster_size_bytes as usize,
        )?;
        let backing = lease.chunk().as_ptr() as *mut DelugeChunkBacking;
        // Never wait for the fill: blocking here would deadlock a single-threaded executor against
        // the drain it waits on. The caller retries on its next tick.
        // SAFETY: `backing` is the chunk just `request`ed, still leased here.
        unsafe { deluge_streaming_request_chunk(backing) };
        if !resource.is_ready(lease.chunk()) {
            return None; // The queue's own lease keeps the fill alive.
        }
        Some(lease)
    }

    /// Makes cluster `index` resident, dropping the lease at once (only the reader's own cluster is
    /// pinned). Returns whether `index` is now resident.
    ///
    /// On a miss this runs `index`'s `native_finish`, whose `stitch_prev` copies its head bytes
    /// into its predecessor's trailing slack. A cache hit does not re-run the stitch; see
    /// [`Self::window`].
    fn ensure_neighbour_resident(&self, index: u32) -> bool {
        self.acquire_and_fill(index).is_some()
    }

    /// Returns the contiguous run of converted frames at the cursor as `(pointer, frame_count)`,
    /// counting whole frames in the reader's `direction`.
    ///
    /// `byte_offset >= resident_bytes` is end of audio: `{null, 0}` with `ok` untouched. A failure
    /// to acquire this cluster, or a fill still in flight, sets `ok = false` and returns
    /// `{null, 0}`. Otherwise the cluster's lease replaces any held one (one pin at a time).
    ///
    /// # Frames straddling a cluster boundary
    ///
    /// When `frame_stride` does not divide the cluster size (24-bit audio against a power-of-two
    /// cluster), the last frame of a non-final cluster spills into the next. Each chunk carries 7
    /// bytes of trailing slack past `cluster_size_bytes` (`payload_with_trailing_slack()`,
    /// `cluster.h`), which `native_finish` keeps equal to the next cluster's head: whichever of two
    /// adjacent clusters finishes second copies across (`stitch_next` or `stitch_prev`), so a
    /// boundary between two resident clusters is stitched in either fill order. The overhang is at
    /// most `frame_stride - 1` bytes, always under 7.
    ///
    /// So when the run includes a straddling frame of real audio (this cluster is full and the next
    /// has data), `window` pins this cluster, then `ensure_neighbour_resident`s the next,
    /// and reads the overhang from the slack. Otherwise the partial tail is past the end and is
    /// dropped. Only the frame nearest the boundary can straddle: the last of a forward run, or the
    /// one at the cursor in a backward run. No frame straddles into a cluster from its predecessor,
    /// as the front guard holds don't-care bytes.
    ///
    /// If the neighbour cannot be made resident, the straddling frame is dropped rather than
    /// failing the reader; a backward run moves its pointer back one frame, since the dropped frame
    /// is the one it points at. If that leaves no frame, `ok = false`: real audio exists but could
    /// not be served, which `deluge_sample_reader_ok` keeps distinct from EOF.
    ///
    /// Known gap: if the next cluster was already resident from an unrelated earlier fill, the
    /// cache hit does not re-run the stitch. This is a property of the stitch mechanism that the
    /// C++ consumers share, not of this reader.
    pub fn window(&mut self) -> (*const u8, u32) {
        // Judge each call fresh so a transient failure does not latch the reader dead; malformed
        // geometry just fails its cheap check again.
        self.ok = true;
        let Some((cluster_index, byte_offset, resident)) =
            locate(self.current_frame, &self.geometry)
        else {
            self.ok = false;
            return (core::ptr::null(), 0);
        };
        if byte_offset >= resident {
            return (core::ptr::null(), 0); // End-of-audio; not a failure.
        }
        let frame_stride = self.geometry.byte_depth as u32 * self.geometry.num_channels as u32;
        if frame_stride == 0 {
            self.ok = false;
            return (core::ptr::null(), 0);
        }

        // A straddle is real audio only for a full cluster whose successor has data.
        let straddle_is_real_audio = || {
            resident == self.geometry.cluster_size_bytes
                && cluster_index
                    .checked_add(1)
                    .map(|next| resident_bytes_for(next, &self.geometry))
                    .unwrap_or(0)
                    > 0
        };

        let (mut frame_count, needs_next) = if self.direction >= 0 {
            let bytes_available = resident - byte_offset;
            let whole = bytes_available / frame_stride;
            let remainder = bytes_available % frame_stride;
            if remainder > 0 && straddle_is_real_audio() {
                (whole + 1, true)
            } else {
                (whole, false)
            }
        } else {
            // Backward: only the frame at the cursor can straddle; earlier ones are further from
            // the cluster's end.
            if byte_offset + frame_stride > resident {
                if straddle_is_real_audio() {
                    (byte_offset / frame_stride + 1, true)
                } else {
                    // The frame at the cursor is not wholly audio: EOF.
                    return (core::ptr::null(), 0);
                }
            } else {
                (byte_offset / frame_stride + 1, false)
            }
        };
        if frame_count == 0 {
            return (core::ptr::null(), 0); // No whole or validly straddling frame: EOF.
        }

        if self.held_cluster != Some(cluster_index) {
            let Some(lease) = self.acquire_and_fill(cluster_index) else {
                self.ok = false;
                self.held_lease = None;
                self.held_cluster = None;
                return (core::ptr::null(), 0);
            };
            self.held_lease = Some(lease);
            self.held_cluster = Some(cluster_index);
        }

        let mut window_byte_offset = byte_offset;
        if needs_next {
            // Cannot overflow: `needs_next` implies `straddle_is_real_audio`'s `checked_add(1)`
            // succeeded.
            if !self.ensure_neighbour_resident(cluster_index + 1) {
                // Drop the unstitched straddling frame rather than fail the reader.
                frame_count -= 1;
                if frame_count == 0 {
                    // It was the only frame: a failure, not EOF.
                    self.ok = false;
                    return (core::ptr::null(), 0);
                }
                if self.direction < 0 {
                    // Backward: the pointer is at the straddling frame, so step it back one frame.
                    // No underflow: a remaining count > 0 means `byte_offset >= frame_stride`.
                    window_byte_offset -= frame_stride;
                }
                // Forward: the straddling frame was last, so the smaller count excludes it.
            }
        }

        // SAFETY: `held_lease` is `Some` and pins a resident, constructed chunk.
        let payload_base = unsafe {
            deluge_sample_fill::chunk::payload(
                self.held_lease.as_ref().unwrap().chunk().as_ptr() as *mut c_void
            )
        };
        // SAFETY: only the frame nearest the boundary can pass `resident`, and only when the
        // neighbour was confirmed resident. Its last byte is at most `cluster_size_bytes +
        // frame_stride - 2`, inside the `cluster_size_bytes + 7` slack span for every real
        // `frame_stride` (<= 8). When the neighbour failed, that frame is excluded, so every
        // exposed byte is within `[0, resident)`.
        let frames = unsafe { payload_base.add(window_byte_offset as usize) };
        (frames as *const u8, frame_count)
    }

    /// Moves the cursor `frames` frames in the reader's direction, saturating at 0 and at the
    /// sample's frame count. Leaving the held cluster drops its lease; the next `window()` re-pins.
    pub fn advance(&mut self, frames: u32) {
        let delta = frames as i64 * self.direction as i64;
        self.current_frame = if delta >= 0 {
            self.current_frame.saturating_add(delta as u64)
        } else {
            self.current_frame.saturating_sub((-delta) as u64)
        };
        if self.ok
            && self.geometry.audio_data_length_bytes != 0
            && self.geometry.audio_data_length_bytes != UNKNOWN_LENGTH_SENTINEL
        {
            let frame_stride = self.geometry.byte_depth as u64 * self.geometry.num_channels as u64;
            if let Some(total_frames) = self
                .geometry
                .audio_data_length_bytes
                .checked_div(frame_stride)
            {
                if self.current_frame > total_frames {
                    self.current_frame = total_frames;
                }
            }
        }
        if let Some(held) = self.held_cluster {
            let still_within = matches!(
                locate(self.current_frame, &self.geometry),
                Some((cluster_index, _, _)) if cluster_index == held
            );
            if !still_within {
                self.held_lease = None; // Releases the lease.
                self.held_cluster = None;
            }
        }
    }

    /// Copies up to `num_frames` native-format frames of `asset` from `start_frame` into `dest`
    /// (`deluge_sample_read`), and returns the number of frames written.
    ///
    /// Drives a forward [`ReadHint::Cached`] reader through `window`/`advance`, so there is one
    /// residency path. Each step is bounded by both `num_frames` and `dest_bytes`. Stops at end of
    /// audio (or a failed `window`), after `num_frames` frames, or when `dest` has no room for a
    /// whole frame. Returns `0` without reading when the asset has no usable geometry.
    ///
    /// # Safety
    ///
    /// `dest` must be valid for `dest_bytes` writable bytes.
    pub unsafe fn read(
        asset: u32,
        start_frame: u64,
        num_frames: u32,
        dest: *mut u8,
        dest_bytes: usize,
    ) -> u32 {
        let mut reader = Reader::open(asset, start_frame, 1, ReadHint::Cached);
        let geo = reader.geometry();
        let stride = geo.byte_depth as usize * geo.num_channels as usize;
        if stride == 0 {
            return 0;
        }

        let mut written_frames: u32 = 0;
        let mut written_bytes: usize = 0;
        while written_frames < num_frames {
            let (frames, frame_count) = reader.window();
            if frame_count == 0 {
                break; // End of audio or a failure.
            }
            let frames_remaining = num_frames - written_frames;
            let max_by_dest = ((dest_bytes - written_bytes) / stride) as u32;
            let n = frame_count.min(frames_remaining).min(max_by_dest);
            if n == 0 {
                break; // No room for a whole frame.
            }
            let copy_bytes = n as usize * stride;
            // SAFETY: `frames` is valid for `frame_count * stride` bytes and `n <= frame_count`;
            // `max_by_dest` keeps the write within `dest_bytes`, which `dest` covers per this
            // function's contract.
            unsafe {
                core::ptr::copy_nonoverlapping(frames, dest.add(written_bytes), copy_bytes);
            }
            reader.advance(n);
            written_frames += n;
            written_bytes += copy_bytes;
        }
        written_frames
    }
}

/// Returns the resident, converted frames at `start_frame` of `source_id` without loading
/// anything (`deluge_sample_peek`): a zero-copy run to the containing cluster's boundary in
/// `direction`.
///
/// Residency is signalled by the pointer, not the count. `frames` is null when there is no manager,
/// no or malformed geometry, `start_frame` is past the audio, or the cluster is not resident and
/// ready. A non-null `frames` with `frame_count == 0` means the cursor is on a final partial frame;
/// callers read it with their own byte math (as `Sample::getAveragesForCrossfade` does).
///
/// Takes no lease, bumps no recency, triggers no load and touches no other cluster, so it is safe
/// on the render thread. For the same reason it never serves a frame straddling into the next
/// cluster: that needs [`Reader::window`]'s pin and neighbour fill.
///
/// `frames` points at `start_frame` in both directions. A forward run extends to higher addresses;
/// a backward run extends to lower ones, down to the cluster's first frame.
pub fn peek(source_id: u32, start_frame: u64, direction: i8) -> (*const u8, u32) {
    // SAFETY: no preconditions; returns the process-wide manager or null.
    let manager = unsafe { deluge_streaming_resource_manager() };
    if manager.is_null() {
        return (core::ptr::null(), 0);
    }
    let Some(ctx) = deluge_sample_fill::fill_context_for(source_id) else {
        return (core::ptr::null(), 0); // No registered geometry.
    };
    let geometry = geometry_from_context(ctx);
    let Some((cluster_index, byte_offset, resident)) = locate(start_frame, &geometry) else {
        return (core::ptr::null(), 0); // Malformed geometry.
    };
    if byte_offset >= resident {
        return (core::ptr::null(), 0); // Past the end of the audio.
    }
    // Non-zero: `locate` rejected a zero stride.
    let frame_stride = geometry.byte_depth as u32 * geometry.num_channels as u32;

    // SAFETY: `manager` is non-null and lives for the rest of the process.
    let resource = unsafe { Resource::from_handle(manager as *mut DelugeResource) };
    let Some(chunk) = resource.peek(source_id, cluster_index) else {
        return (core::ptr::null(), 0); // Not resident.
    };
    if !resource.is_ready(chunk) {
        return (core::ptr::null(), 0); // Resident but not yet loaded.
    }

    // Whole frames only, within this cluster: forward up to `resident`, backward down to the
    // cluster's first frame (including the one at `byte_offset`).
    let frame_count = if direction >= 0 {
        (resident - byte_offset) / frame_stride
    } else {
        byte_offset / frame_stride + 1
    };
    // No early return on a zero count: a final partial frame is still a resident position.

    // SAFETY: `chunk` is resident and ready, and stays so for this call, which never blocks,
    // yields or evicts.
    let payload_base = unsafe { deluge_sample_fill::chunk::payload(chunk.as_ptr() as *mut c_void) };
    // SAFETY: `byte_offset < resident <= cluster_size_bytes`, and a forward run ends at or before
    // `resident`, so every exposed byte is inside the cluster payload.
    let frames = unsafe { payload_base.add(byte_offset as usize) };
    (frames as *const u8, frame_count)
}

/// Invalidates every resident cluster of `source_id` (`deluge_sample_invalidate`): flags each
/// unloadable, so an in-flight fill cannot publish stale bytes, and removes it from the load queue.
///
/// Mirrors C++ `Sample::markAsUnloadable`, with the cluster span computed from the fill-context. A
/// no-op without a registered fill-context or with an unknown length (see below).
pub fn invalidate(source_id: u32) {
    // SAFETY: no preconditions; returns the process-wide manager or null.
    let manager = unsafe { deluge_streaming_resource_manager() };
    if manager.is_null() {
        return;
    }
    let Some(ctx) = deluge_sample_fill::fill_context_for(source_id) else {
        return; // No registered geometry.
    };
    if ctx.cluster_size == 0 || ctx.cluster_size_magnitude >= 64 {
        return; // Malformed geometry.
    }
    // An unknown (recording) or zero length has no finite cluster count here; C++ uses the
    // recorder's live count, which this module cannot see. Bail out rather than scan ~4.3 billion
    // clusters.
    if ctx.audio_data_length_bytes == 0 || ctx.audio_data_length_bytes == UNKNOWN_LENGTH_SENTINEL {
        return;
    }
    // `ceil((start + length) / cluster_size)` clusters, from 0 rather than the first audio cluster:
    // a header cluster can be resident from the load-time parse, and `markAsUnloadable`
    // invalidates it too.
    let total_bytes = ctx.audio_data_start_pos_bytes as u64 + ctx.audio_data_length_bytes;
    let num_clusters = total_bytes
        .div_ceil(ctx.cluster_size as u64)
        .min(u32::MAX as u64) as u32;
    // SAFETY: `manager` is non-null and lives for the rest of the process.
    let resource = unsafe { Resource::from_handle(manager as *mut DelugeResource) };
    for index in 0..num_clusters {
        if let Some(chunk) = resource.peek(source_id, index) {
            // SAFETY: `chunk` is a live resident backing; the setter writes only its `unloadable`
            // byte.
            unsafe { deluge_sample_fill::chunk::set_unloadable(chunk.as_ptr() as *mut c_void) };
            resource.loader_remove(resource.slot_of(chunk));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host_streaming_stubs::{set_active_manager, TEST_LOCK};
    use alloc::boxed::Box;
    use deluge_resource::facade::Resource;
    use deluge_resource::value::COST_IO;
    use deluge_resource::DelugeResource;
    use deluge_sample_fill::{deluge_streaming_set_fill_context, FillContext};
    extern crate std;
    use core::ffi::c_void;
    use std::vec::Vec;

    const CHUNK_SIZE: usize = 4096;

    /// Seeds a per-index ramp. The lifecycle tests never read it; `Resource::request` just needs a
    /// construct callback to mint a real `Lease`.
    unsafe extern "C" fn make_ramp_construct(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        index: u32,
        dest: *mut u8,
    ) {
        for b in 0..CHUNK_SIZE {
            // SAFETY: `dest` is the manager's `CHUNK_SIZE`-byte backing (`ConstructFn`'s contract).
            unsafe {
                *dest.add(b) = index.wrapping_add(b as u32) as u8;
            }
        }
    }

    /// Backing arena for the leaked test heap.
    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// A `FillContext` for `CHUNK_SIZE` clusters, enough for `Reader::open` to be ok. These tests
    /// never call `window()`, so the other values do not matter.
    fn lifecycle_fill_context() -> FillContext {
        FillContext {
            efatfs_handle: 0,
            audio_data_start_pos_bytes: 0,
            audio_data_length_bytes: 0, // Unbounded for `resident_bytes_for`.
            first_cluster_index_with_no_audio_data: -1,
            cluster_size: CHUNK_SIZE as u32,
            cluster_size_magnitude: 12, // 2^12 == 4096 == CHUNK_SIZE
            raw_data_format: 0,
            byte_depth: 2,
            num_channels: 1,
        }
    }

    /// Builds a manager over a leaked test heap with one requestable asset, registers its
    /// fill-context and makes it the active manager. Callers must hold [`TEST_LOCK`].
    fn test_manager_and_asset() -> (*mut DelugeResource, u32) {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf: Vec<u128> = std::vec![0u128; words];
        let ptr = buf.as_mut_ptr() as *mut u8;
        // SAFETY: `buf` is leaked below, so the arena outlives the test.
        let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
        std::mem::forget(TestHeap { _buf: buf });
        // SAFETY: `h` is live.
        let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        // SAFETY: `handle` is live.
        let asset = unsafe {
            deluge_resource::deluge_resource_define_asset(
                handle,
                core::ptr::null_mut(),
                None,
                None,
                core::ptr::null_mut(),
                COST_IO,
                deluge_resource::manager::BACKING_HEAP,
            )
        };
        unsafe {
            deluge_resource::deluge_resource_set_construct(
                handle,
                asset,
                Some(make_ramp_construct),
            );
        }
        deluge_streaming_set_fill_context(core::ptr::null_mut(), asset, lifecycle_fill_context());
        set_active_manager(handle as *mut c_void);
        (handle, asset)
    }

    #[test]
    fn open_sets_the_expected_initial_state_with_no_lease_held() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (_handle, asset) = test_manager_and_asset();
        let reader = Reader::open(asset, 123, -1, ReadHint::Scan);
        assert_eq!(reader.asset(), asset);
        assert_eq!(reader.current_frame(), 123);
        assert_eq!(reader.direction(), -1);
        assert_eq!(reader.hint(), ReadHint::Scan);
        assert!(reader.held_lease.is_none(), "open must not take a lease");
        assert!(
            reader.ok(),
            "geometry must resolve from the registered fill-context (Step 0)"
        );
    }

    /// Uses a real manager lease attached to `held_lease`, as `window()` would.
    #[test]
    fn seek_drops_the_held_lease_and_updates_current_frame() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (handle, asset) = test_manager_and_asset();
        // SAFETY: `handle` is live for the test's duration.
        let resource = unsafe { Resource::from_handle(handle) };

        let mut reader = Reader::open(asset, 10, 1, ReadHint::Cached);
        assert_eq!(reader.current_frame(), 10);

        let lease = resource.request(asset, 0, CHUNK_SIZE).expect("request");
        let slot = resource.slot_of(lease.chunk());
        assert_eq!(
            resource.lease_count_by_slot(slot),
            1,
            "the fresh request holds exactly one lease before seek"
        );
        reader.held_lease = Some(lease);

        reader.seek(42);

        assert_eq!(reader.current_frame(), 42, "seek must move the cursor");
        assert_eq!(
            resource.lease_count_by_slot(slot),
            0,
            "seek must release the held lease"
        );
    }

    #[test]
    fn seek_with_no_held_lease_just_moves_the_cursor() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (_handle, asset) = test_manager_and_asset();
        let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
        reader.seek(7);
        assert_eq!(reader.current_frame(), 7);
    }

    /// `deluge_sample_reader_close` drops the `Box<Reader>`; this checks that at the `Box` level.
    #[test]
    fn boxed_open_is_non_null_and_dropping_it_releases_any_held_lease_without_leak() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (handle, asset) = test_manager_and_asset();
        // SAFETY: `handle` is live for the test's duration.
        let resource = unsafe { Resource::from_handle(handle) };

        let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
        let lease = resource.request(asset, 1, CHUNK_SIZE).expect("request");
        let slot = resource.slot_of(lease.chunk());
        reader.held_lease = Some(lease);
        assert_eq!(resource.lease_count_by_slot(slot), 1);

        let ptr: *mut Reader = Box::into_raw(Box::new(reader));
        assert!(!ptr.is_null(), "open (boxed) must be non-null");

        // SAFETY: `ptr` came from `Box::into_raw` above and has not been freed.
        unsafe { drop(Box::from_raw(ptr)) }; // As `deluge_sample_reader_close` does.

        assert_eq!(
            resource.lease_count_by_slot(slot),
            0,
            "close must release the held lease, with no leak"
        );
    }

    /// The same cases as `deluge_sample_source::geometry::resident_bytes_for`'s test, checking the
    /// mirror matches.
    #[test]
    fn resident_bytes_for_full_clusters_short_last_and_sentinel() {
        fn geo(len: u64) -> Geometry {
            Geometry {
                audio_data_start_bytes: 0,
                audio_data_length_bytes: len,
                cluster_size_bytes: 16,
                byte_depth: 2,
                num_channels: 1,
                raw_data_format: 0,
            }
        }
        let g = geo(68); // Clusters 0-3 full, cluster 4 has 4 bytes.
        assert_eq!(resident_bytes_for(0, &g), 16);
        assert_eq!(resident_bytes_for(3, &g), 16);
        assert_eq!(resident_bytes_for(4, &g), 4);
        assert_eq!(resident_bytes_for(5, &g), 0);
        let s = geo(UNKNOWN_LENGTH_SENTINEL);
        assert_eq!(resident_bytes_for(99, &s), 16);
        let z = geo(0);
        assert_eq!(resident_bytes_for(0, &z), 16);
    }

    // `window`/`advance` over a real synthetic fill.
    //
    // A synthetic sample: 512-byte clusters, 16-bit mono (256 frames per cluster), native format,
    // so `native_finish` leaves the payload alone and the read stub's ramp (`byte_offset + i`)
    // reaches `window()` untouched. The manager, `native_begin`/`native_finish` and chunks are all
    // real: every backing is a `StreamedChunk` built by the production construct callback. That is
    // needed for soundness too: `native_finish` reborrows the backing as `&mut StreamedChunk`.
    mod window_tests {
        use super::*;
        use crate::host_streaming_stubs::{set_fail_at_byte_offset, set_force_read_failure};

        // The smallest size that reads anything: the fill counts 512-byte sectors
        // (`cluster_size >> 9`), so a smaller cluster reads zero sectors.
        const CLUSTER_SIZE: u32 = 512;
        const CLUSTER_MAGNITUDE: u32 = 9; // 2^9 == 512

        /// Real per-chunk backing: the `StreamedChunk` header, the payload, and the 7 bytes of
        /// trailing slack `native_finish` touches, rounded up to the slab's 16-byte slots. Derived
        /// from the real offset so it tracks `StreamedChunk`'s layout.
        const BACKING_SIZE: usize =
            (deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET + CLUSTER_SIZE as usize + 7)
                .next_multiple_of(16);

        /// Placement-constructs a real `StreamedChunk` at `dest` with the production construct
        /// callback. The wrapper only adapts `ConstructFn`'s `*mut u8` to its `*mut c_void`.
        ///
        /// # Safety
        ///
        /// `dest` must be writable for [`BACKING_SIZE`] bytes.
        unsafe extern "C" fn real_chunk_construct(
            ctx: *mut c_void,
            owner: *mut c_void,
            index: u32,
            dest: *mut u8,
        ) {
            // SAFETY: the caller's contract is the real construct's.
            unsafe {
                deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
                    ctx,
                    owner,
                    index,
                    dest as *mut c_void,
                );
            }
        }

        struct TestHeap {
            _buf: Vec<u128>,
        }

        fn geo(audio_data_length_bytes: u64) -> FillContext {
            FillContext {
                efatfs_handle: 0,
                audio_data_start_pos_bytes: 0,
                audio_data_length_bytes,
                first_cluster_index_with_no_audio_data: -1,
                cluster_size: CLUSTER_SIZE,
                cluster_size_magnitude: CLUSTER_MAGNITUDE,
                raw_data_format: 0, // Native
                byte_depth: 2,
                num_channels: 1,
            }
        }

        /// Builds a manager and a slab-backed asset (as production's streaming assets are; the slab
        /// fixes the slot size, so the `size` passed to `request` is ignored), registers `ctx` and
        /// makes it the active manager. Callers must hold [`TEST_LOCK`].
        fn harness(ctx: FillContext) -> (*mut DelugeResource, u32) {
            let words = (256 * 1024usize).div_ceil(16);
            let mut buf: Vec<u128> = std::vec![0u128; words];
            let ptr = buf.as_mut_ptr() as *mut u8;
            // SAFETY: `buf` is leaked below, so the arena outlives the test.
            let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
            std::mem::forget(TestHeap { _buf: buf });
            // SAFETY: `h` is live. The tests keep at most 2 clusters resident.
            let slab =
                unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, BACKING_SIZE, 16) };
            assert!(!slab.is_null());
            // SAFETY: `h` is live.
            let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
            assert!(!handle.is_null());
            // SAFETY: `handle` and `slab` are live, over the same heap.
            unsafe { deluge_resource::deluge_resource_set_slab(handle, slab) };
            // SAFETY: `handle` is live.
            let asset = unsafe {
                deluge_resource::deluge_resource_define_asset(
                    handle,
                    core::ptr::null_mut(),
                    None,
                    None,
                    core::ptr::null_mut(),
                    COST_IO,
                    deluge_resource::manager::BACKING_SLAB,
                )
            };
            unsafe {
                deluge_resource::deluge_resource_set_construct(
                    handle,
                    asset,
                    Some(real_chunk_construct),
                );
            }
            deluge_streaming_set_fill_context(core::ptr::null_mut(), asset, ctx);
            set_active_manager(handle as *mut c_void);
            (handle, asset)
        }

        /// Returns the bytes the read stub fills cluster `index` with: `(index * CLUSTER_SIZE + i)
        /// as u8`.
        fn expected_cluster_bytes(index: u32) -> Vec<u8> {
            let base = index * CLUSTER_SIZE;
            (0..CLUSTER_SIZE)
                .map(|i| base.wrapping_add(i) as u8)
                .collect()
        }

        #[test]
        fn window_at_frame_zero_matches_the_synthetic_fill_and_reports_frames_to_backing_end() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64)); // 3 full clusters

            let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
            assert!(
                reader.ok(),
                "geometry must resolve from the registered fill-context"
            );

            let (ptr, frame_count) = reader.window();
            assert!(
                !ptr.is_null(),
                "window must return a real pointer on a successful fill"
            );
            assert_eq!(
                frame_count,
                CLUSTER_SIZE / 2,
                "256 frames of stride 2 fill the whole 512-byte cluster"
            );

            // SAFETY: `reader`'s held lease pins the chunk until the end of this scope.
            let got: Vec<u8> = (0..CLUSTER_SIZE as usize)
                .map(|i| unsafe { *ptr.add(i) })
                .collect();
            assert_eq!(
                got,
                expected_cluster_bytes(0),
                "window's bytes must match the synthetic fill's converted data \
                 (Native format -> convert/stitch is a no-op over it)"
            );
            assert!(reader.ok(), "a successful window must not clear ok()");
        }

        #[test]
        fn window_past_the_last_audio_frame_is_eof_not_a_failure() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            // 10 bytes of real audio (5 frames of stride 2): cluster 0 is short.
            let (_handle, asset) = harness(geo(10));
            // Frame 5 is byte 10: exactly the end of the audio.
            let mut reader = Reader::open(asset, 5, 1, ReadHint::Cached);
            assert!(reader.ok());

            let (ptr, frame_count) = reader.window();
            assert!(ptr.is_null(), "EOF must return a null pointer");
            assert_eq!(frame_count, 0, "EOF must report frame_count == 0");
            assert!(
                reader.ok(),
                "end-of-audio is the ordinary, expected way a scan terminates, not a failure"
            );
        }

        #[test]
        fn window_self_pins_and_advance_releases_across_a_boundary_but_not_within_it() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            let (handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));
            // SAFETY: `handle` is live for the test's duration.
            let resource = unsafe { Resource::from_handle(handle) };

            let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
            let (ptr0, frame_count0) = reader.window();
            assert!(!ptr0.is_null());
            assert_eq!(frame_count0, CLUSTER_SIZE / 2);

            let chunk0 = reader
                .held_lease
                .as_ref()
                .expect("window must self-pin the backing it points into")
                .chunk();
            let slot0 = resource.slot_of(chunk0);
            assert_eq!(
                resource.lease_count_by_slot(slot0),
                1,
                "the manager must report exactly one lease held on the backing"
            );

            // Advance within cluster 0 (256 frames wide): the pin must be retained, not re-taken.
            reader.advance(5);
            assert_eq!(reader.current_frame(), 5);
            assert!(
                reader.held_lease.is_some(),
                "advance within the backing must keep the pin"
            );
            assert_eq!(resource.lease_count_by_slot(slot0), 1);

            // Advance to cluster 1's first frame (256, byte 512): the old pin is released as soon
            // as the cursor leaves it, before the next window() re-pins.
            reader.advance(251);
            assert_eq!(reader.current_frame(), 256);
            assert!(
                reader.held_lease.is_none(),
                "advance past the backing must release the pin (self-pin invariant)"
            );
            assert_eq!(
                resource.lease_count_by_slot(slot0),
                0,
                "the released pin must drop the manager's lease count for cluster 0"
            );

            // The next window() re-pins on the new cluster and returns the frames there.
            let (ptr1, frame_count1) = reader.window();
            assert!(!ptr1.is_null());
            assert_eq!(
                frame_count1,
                CLUSTER_SIZE / 2,
                "cluster 1 at its own offset 0 is a fresh full cluster"
            );
            let got: Vec<u8> = (0..CLUSTER_SIZE as usize)
                .map(|i| unsafe { *ptr1.add(i) })
                .collect();
            assert_eq!(
                got,
                expected_cluster_bytes(1),
                "cluster 1's window bytes must match the synthetic fill"
            );
        }

        #[test]
        fn forced_read_failure_clears_ok_distinct_from_eof() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));
            set_force_read_failure(true);

            let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
            assert!(
                reader.ok(),
                "open itself must still succeed (geometry resolves)"
            );

            let (ptr, frame_count) = reader.window();
            assert!(ptr.is_null());
            assert_eq!(frame_count, 0);
            assert!(
                !reader.ok(),
                "a forced read failure must clear ok() -- distinct from EOF, which leaves it set"
            );

            set_force_read_failure(false); // restore for other tests sharing this thread
            set_fail_at_byte_offset(None);
        }

        #[test]
        fn window_recovers_after_a_transient_read_failure() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

            set_force_read_failure(true);
            let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
            let (ptr, _) = reader.window();
            assert!(ptr.is_null());
            assert!(!reader.ok(), "a transient read failure clears ok()");

            // The card recovers: the next window() must retry, not stay latched dead.
            set_force_read_failure(false);
            let (ptr2, frame_count2) = reader.window();
            assert!(
                !ptr2.is_null(),
                "reader recovers on the next window after the failure clears"
            );
            assert!(frame_count2 > 0);
            assert!(reader.ok(), "ok() reflects the now-successful window");

            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
        }

        // Boundary straddles.
        //
        // 24-bit mono (`frame_stride == 3`) does not divide `CLUSTER_SIZE` (512 % 3 == 2). Frame
        // 170 (bytes 510..513) straddles clusters 0 and 1: byte 512 exists only once the stitch
        // copies cluster 1's head into cluster 0's trailing slack. The ramp wraps 512 to 0, so the
        // frame's expected bytes are `[510, 511, 0]` as `u8`.

        /// A 24-bit mono geometry (`frame_stride == 3`).
        fn geo24(audio_data_length_bytes: u64) -> FillContext {
            FillContext {
                efatfs_handle: 0,
                audio_data_start_pos_bytes: 0,
                audio_data_length_bytes,
                first_cluster_index_with_no_audio_data: -1,
                cluster_size: CLUSTER_SIZE,
                cluster_size_magnitude: CLUSTER_MAGNITUDE,
                raw_data_format: 0, // Native
                byte_depth: 3,
                num_channels: 1,
            }
        }

        #[test]
        fn window_straddles_a_cluster_boundary_forward_with_24bit_stride() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            // 3 full clusters, so the straddle at cluster 0's end is real audio.
            let (_handle, asset) = harness(geo24(3 * CLUSTER_SIZE as u64));

            // Frame 170 is cluster 0, offset 510: 2 bytes left, less than a frame.
            let mut reader = Reader::open(asset, 170, 1, ReadHint::Cached);
            assert!(reader.ok());

            let (ptr, frame_count) = reader.window();
            assert!(
                !ptr.is_null(),
                "a mid-stream straddling frame must NOT report EOF"
            );
            assert_eq!(
                frame_count, 1,
                "exactly the one straddling frame is servable from cluster 0's \
                 (now-stitched) trailing slack -- not a false EOF"
            );
            assert!(reader.ok(), "a straddling-but-real frame is not a failure");

            // SAFETY: bytes 510 and 511 are in cluster 0's pinned payload, 512 in its trailing
            // slack.
            let got = unsafe { [*ptr, *ptr.add(1), *ptr.add(2)] };
            assert_eq!(
                got,
                [254u8, 255u8, 0u8],
                "bytes 510/511 are cluster 0's own ramp; byte 512 is cluster 1's stitched head[0]"
            );
        }

        #[test]
        fn window_straddles_a_cluster_boundary_backward_with_24bit_stride() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            let (_handle, asset) = harness(geo24(3 * CLUSTER_SIZE as u64));

            // The same frame 170, backward: only the cursor's own frame straddles.
            let mut reader = Reader::open(asset, 170, -1, ReadHint::Cached);
            assert!(reader.ok());

            let (ptr, frame_count) = reader.window();
            assert!(
                !ptr.is_null(),
                "a mid-stream straddling frame must NOT report EOF (backward)"
            );
            assert_eq!(
                frame_count, 171,
                "frames 0..=170 (byte_offset/stride + 1) are all safely servable backward \
                 from here, including the straddling frame at the cursor"
            );
            assert!(reader.ok());

            // SAFETY: as in the forward test; the pointer is at the same frame.
            let got = unsafe { [*ptr, *ptr.add(1), *ptr.add(2)] };
            assert_eq!(got, [254u8, 255u8, 0u8]);
        }

        #[test]
        fn window_true_eof_with_24bit_stride_does_not_falsely_straddle() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            // 511 bytes of audio: cluster 0 is short and there is no cluster 1, so no straddle.
            let (_handle, asset) = harness(geo24(511));

            // Frame 170 is offset 510 with 1 byte left: no whole frame remains.
            let mut reader = Reader::open(asset, 170, 1, ReadHint::Cached);
            assert!(reader.ok());

            let (ptr, frame_count) = reader.window();
            assert!(ptr.is_null(), "true EOF must return a null pointer");
            assert_eq!(
                frame_count, 0,
                "the short last cluster's partial tail must NOT be falsely counted as a \
                 straddling frame"
            );
            assert!(
                reader.ok(),
                "genuine end-of-audio is not a failure, even with a non-dividing stride"
            );
        }

        #[test]
        fn advance_saturates_at_total_frames_near_eof() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64)); // byte_depth 2 -> stride 2
            let total_frames = (3 * CLUSTER_SIZE as u64) / 2; // 768

            let mut reader = Reader::open(asset, total_frames - 10, 1, ReadHint::Cached);
            reader.advance(u32::MAX); // a huge forward advance from near-EOF
            assert_eq!(
                reader.current_frame(),
                total_frames,
                "advance must clamp at the sample's own total frame count, not overrun/wrap past it"
            );
        }

        // A failed neighbour fill must neither expose an unconfirmed straddling frame nor look
        // like EOF. `set_fail_at_byte_offset` fails only the neighbour, so the reader's own
        // cluster still fills.

        #[test]
        fn window_backward_degrade_retreats_the_pointer_past_the_unconfirmed_straddling_frame() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            let (_handle, asset) = harness(geo24(3 * CLUSTER_SIZE as u64));

            // Fail cluster 1's read (byte 512); cluster 0's succeeds.
            set_fail_at_byte_offset(Some(CLUSTER_SIZE));

            // Frame 170: cluster 0, offset 510, 171 frames before the degrade.
            let mut reader = Reader::open(asset, 170, -1, ReadHint::Cached);
            assert!(reader.ok());

            let (ptr, frame_count) = reader.window();
            assert!(
                !ptr.is_null(),
                "plenty of SAFE frames remain even though the straddling one had to be dropped"
            );
            assert_eq!(
                frame_count, 170,
                "171 (byte_offset/stride + 1) minus the one dropped, unconfirmed straddling frame"
            );
            assert!(
                reader.ok(),
                "frames remain servable -- this is a degrade, not a failure"
            );

            // `ptr` must have stepped back from the straddling frame (510..513) to 507..510, wholly
            // inside cluster 0, rather than expose unconfirmed bytes.
            // SAFETY: 507..510 is inside cluster 0's pinned plain payload.
            let got = unsafe { [*ptr, *ptr.add(1), *ptr.add(2)] };
            assert_eq!(
                got,
                [251u8, 252u8, 253u8],
                "ptr must land on bytes 507..510 (cluster 0's own ramp) -- if this instead reads \
                 [254, 255, <anything>] (bytes 510..513, the straddling frame), the degrade path \
                 failed to retreat the pointer and is still exposing the unconfirmed frame"
            );
        }

        #[test]
        fn window_forward_degrade_to_zero_is_a_failure_not_eof() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            // A 4-byte cluster, so the straddling frame can be the only servable one. The fill
            // reads zero sectors, which is fine: this test never inspects bytes.
            const TINY_CLUSTER: u32 = 4;
            const TINY_MAGNITUDE: u32 = 2; // 2^2 == 4
            let ctx = FillContext {
                efatfs_handle: 0,
                audio_data_start_pos_bytes: 0,
                audio_data_length_bytes: 3 * TINY_CLUSTER as u64, // 3 full 4-byte clusters
                first_cluster_index_with_no_audio_data: -1,
                cluster_size: TINY_CLUSTER,
                cluster_size_magnitude: TINY_MAGNITUDE,
                raw_data_format: 0, // Native
                byte_depth: 3,
                num_channels: 1,
            };
            let (_handle, asset) = harness(ctx);

            // Fail cluster 1's read (byte 4); cluster 0's succeeds.
            set_fail_at_byte_offset(Some(TINY_CLUSTER));

            // Frame 1 is cluster 0, offset 3: a straddle, and the only frame here.
            let mut reader = Reader::open(asset, 1, 1, ReadHint::Cached);
            assert!(reader.ok());

            let (ptr, frame_count) = reader.window();
            assert!(ptr.is_null());
            assert_eq!(frame_count, 0);
            assert!(
                !reader.ok(),
                "the straddling frame was the ONLY servable one and couldn't be confirmed -- a \
                 failure (retry later), not true end-of-audio, which real audio here is not"
            );
        }

        #[test]
        fn window_backward_degrade_to_zero_is_a_failure_not_eof() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            // The same 4-byte clusters as the forward version.
            const TINY_CLUSTER: u32 = 4;
            const TINY_MAGNITUDE: u32 = 2; // 2^2 == 4
            let ctx = FillContext {
                efatfs_handle: 0,
                audio_data_start_pos_bytes: 0,
                audio_data_length_bytes: 3 * TINY_CLUSTER as u64, // 3 full 4-byte clusters
                first_cluster_index_with_no_audio_data: -1,
                cluster_size: TINY_CLUSTER,
                cluster_size_magnitude: TINY_MAGNITUDE,
                raw_data_format: 0, // Native
                byte_depth: 3,
                num_channels: 1,
            };
            let (_handle, asset) = harness(ctx);

            // Fail cluster 2's read (byte 8); cluster 1's (byte 4) succeeds.
            set_fail_at_byte_offset(Some(2 * TINY_CLUSTER));

            // Frame 2 is cluster 1, offset 2: a straddle, and the only frame backward from here.
            let mut reader = Reader::open(asset, 2, -1, ReadHint::Cached);
            assert!(reader.ok());

            let (ptr, frame_count) = reader.window();
            assert!(ptr.is_null());
            assert_eq!(frame_count, 0);
            assert!(
                !reader.ok(),
                "the straddling frame at the cursor was the ONLY servable one and couldn't be \
                 confirmed -- a failure (retry later), not true end-of-audio"
            );
        }

        // `Reader::read`, nested here to reuse this module's harness.
        mod read_tests {
            use super::*;

            const STRIDE: usize = 2; // `geo()`: 16-bit mono.

            #[test]
            fn read_matches_a_manual_open_window_advance_loop_across_a_cluster_boundary() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                // 300 frames: all of cluster 0 and 44 of cluster 1, crossing a re-pin.
                let num_frames: u32 = 300;
                let dest_bytes = num_frames as usize * STRIDE;

                let mut via_read = std::vec![0xAAu8; dest_bytes];
                // SAFETY: `via_read` is exactly `dest_bytes` bytes long.
                let written = unsafe {
                    Reader::read(asset, 0, num_frames, via_read.as_mut_ptr(), dest_bytes)
                };
                assert_eq!(
                    written, num_frames,
                    "plenty of real audio exists (768 frames) -- the whole range is servable"
                );

                // The same range, driven by hand through `window`/`advance`.
                let mut manual: Vec<u8> = Vec::with_capacity(dest_bytes);
                let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
                let mut remaining = num_frames;
                while remaining > 0 {
                    let (ptr, frame_count) = reader.window();
                    if frame_count == 0 {
                        break;
                    }
                    let n = frame_count.min(remaining);
                    for i in 0..(n as usize * STRIDE) {
                        // SAFETY: `ptr` is valid for `frame_count * STRIDE` bytes and
                        // `i < n * STRIDE <= frame_count * STRIDE`.
                        manual.push(unsafe { *ptr.add(i) });
                    }
                    reader.advance(n);
                    remaining -= n;
                }

                assert_eq!(
                    via_read, manual,
                    "deluge_sample_read must be byte-identical to a manual open+window+memcpy+\
                     advance loop over the same range"
                );
            }

            #[test]
            fn read_past_end_of_audio_returns_a_short_count_and_does_not_overrun_dest() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                // 10 bytes of audio: 5 frames; cluster 0 is short.
                let (_handle, asset) = harness(geo(10));

                let num_frames: u32 = 100; // far past the 5 real frames
                let dest_bytes = num_frames as usize * STRIDE;
                let mut dest = std::vec![0xAAu8; dest_bytes];

                // SAFETY: `dest` is exactly `dest_bytes` bytes long.
                let written =
                    unsafe { Reader::read(asset, 0, num_frames, dest.as_mut_ptr(), dest_bytes) };

                assert_eq!(written, 5, "only 5 real frames of audio exist");
                assert_eq!(
                    &dest[0..10],
                    &expected_cluster_bytes(0)[0..10],
                    "the 5 real frames actually written must match the synthetic fill"
                );
                assert!(
                    dest[10..].iter().all(|&b| b == 0xAA),
                    "must not write a single byte past the real audio into the rest of dest"
                );
            }

            #[test]
            fn read_bounds_the_write_to_dest_bytes_when_smaller_than_num_frames_times_stride() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                // 768 frames of audio, so `dest_bytes` is the only bound.
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                let num_frames: u32 = 300;
                // Odd, and far below `num_frames * STRIDE`: tests the bound and the dropped
                // partial frame. `dest` is exactly this long, so an overrun corrupts the heap.
                let dest_bytes: usize = 101;
                let mut dest = std::vec![0xAAu8; dest_bytes];

                // SAFETY: `dest` is exactly `dest_bytes` bytes long.
                let written =
                    unsafe { Reader::read(asset, 0, num_frames, dest.as_mut_ptr(), dest_bytes) };

                let max_frames = (dest_bytes / STRIDE) as u32; // 50
                assert_eq!(
                    written, max_frames,
                    "dest_bytes (101 -> 50 whole frames) must bound the write, not num_frames (300)"
                );
                let written_bytes = max_frames as usize * STRIDE; // 100
                assert_eq!(
                    &dest[0..written_bytes],
                    &expected_cluster_bytes(0)[0..written_bytes],
                    "the bytes actually written must match the synthetic fill"
                );
                assert_eq!(
                    dest[written_bytes], 0xAA,
                    "the one leftover byte (100..101, less than one whole frame) must be untouched"
                );
            }
        }

        // `peek`, nested here to reuse this module's harness.
        mod peek_tests {
            use super::*;

            const STRIDE: u32 = 2; // `geo()`: 16-bit mono.

            #[test]
            fn peek_forward_from_a_clusters_first_frame_runs_to_its_last_frame_matching_the_ramp() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                // Fill cluster 0 through `window()` and keep `reader` alive, so its lease holds the
                // chunk resident; `peek` takes no pin of its own.
                let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
                let (_, filled) = reader.window();
                assert_eq!(filled, CLUSTER_SIZE / 2);

                let (frames, frame_count) = peek(asset, 0, 1);
                assert!(
                    !frames.is_null(),
                    "a resident+ready cluster must not peek null"
                );
                assert_eq!(
                    frame_count,
                    CLUSTER_SIZE / 2,
                    "a +1 peek from the cluster's own first frame must run all the way to its \
                     last resident frame"
                );
                // SAFETY: `reader`'s held lease pins the chunk.
                let got: Vec<u8> = (0..CLUSTER_SIZE as usize)
                    .map(|i| unsafe { *frames.add(i) })
                    .collect();
                assert_eq!(
                    got,
                    expected_cluster_bytes(0),
                    "peek's bytes must match the synthetic fill, exactly like window()'s own"
                );
            }

            #[test]
            fn peek_backward_at_a_mid_cluster_frame_runs_back_to_the_clusters_first_frame() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
                reader.window(); // fill+ready cluster 0; kept alive (not dropped) below.

                // Frame 100 is byte 200, well inside cluster 0.
                let start_frame = 100u64;
                let byte_offset = 200u32;
                let (frames, frame_count) = peek(asset, start_frame, -1);
                assert!(!frames.is_null());
                assert_eq!(
                    frame_count,
                    byte_offset / STRIDE + 1,
                    "a -1 peek must run back to (and include) the cluster's own first frame"
                );

                // `frames` points at `start_frame`; walking back `k` frames reads the ramp at
                // `byte_offset - k * STRIDE`.
                for k in 0..frame_count {
                    // SAFETY: `k < frame_count` keeps the read inside cluster 0's payload, which
                    // `reader`'s lease pins.
                    let byte = unsafe { *frames.sub(k as usize * STRIDE as usize) };
                    let expected = (byte_offset - k * STRIDE) as u8;
                    assert_eq!(
                        byte, expected,
                        "reverse-run byte at k={k} must match the ramp"
                    );
                }
            }

            #[test]
            fn peek_on_the_last_partial_frame_is_resident_non_null_with_a_zero_count() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                // 511 bytes of 24-bit mono: cluster 0 is short and ends in a partial frame.
                let (_handle, asset) = harness(geo24(511));

                // Fill cluster 0 from frame 0 and keep `reader`'s pin alive.
                let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
                let (ptr, filled) = reader.window();
                assert!(!ptr.is_null(), "cluster 0 must fill from frame 0");
                assert_eq!(filled, 170, "511 bytes / stride 3 == 170 whole frames");

                // Frame 170 is offset 510 with 1 byte left: resident, so non-null with a 0 count.
                let (frames, frame_count) = peek(asset, 170, 1);
                assert!(
                    !frames.is_null(),
                    "the last partial frame is resident: residency is the pointer, not the count"
                );
                assert_eq!(
                    frame_count, 0,
                    "no WHOLE frame fits forward from the last partial frame -- 0, not EOF"
                );

                // `frames` points at byte 510; the ramp gives 254 and 255.
                // SAFETY: `reader`'s lease pins cluster 0; bytes 510 and 511 are within its 511
                // resident bytes.
                let got = unsafe { [*frames, *frames.add(1)] };
                assert_eq!(
                    got,
                    [254u8, 255u8],
                    "frames must point at the cursor's own ramp bytes, ready to read byte-wise"
                );
            }

            #[test]
            fn peek_on_a_non_resident_cluster_returns_null() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                // Geometry is registered, but no cluster was ever requested.
                let (frames, frame_count) = peek(asset, 0, 1);
                assert!(frames.is_null(), "a never-resident cluster must peek null");
                assert_eq!(frame_count, 0);
            }

            #[test]
            fn peek_on_a_resident_but_not_ready_cluster_returns_null() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                let (handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));
                // SAFETY: `handle` is live for the test's duration.
                let resource = unsafe { Resource::from_handle(handle) };

                // `request` makes the chunk resident but not ready (no fill, no `mark_ready`).
                // Held for the whole test.
                let _reservation = resource
                    .request(asset, 0, CLUSTER_SIZE as usize)
                    .expect("request");

                let (frames, frame_count) = peek(asset, 0, 1);
                assert!(
                    frames.is_null(),
                    "resident-but-not-ready must peek null (the Step 2 ready gate)"
                );
                assert_eq!(frame_count, 0);
            }
        }
    }
}
