//! The reader's pure state: [`Reader`], a small frame-cursor over one sample's source residency.
//! `open`/`seek` handle lifecycle, with ordinary `Drop` for close (see `abi.rs`). `open` resolves
//! real geometry, and `window`/`advance` do the actual streaming reads — a cache-hit acquire, a
//! fill requested from the async drain on a miss, and a self-pinning single held lease.

use core::ffi::c_void;

use deluge_resource::facade::Lease;
use deluge_resource::{DelugeResource, Resource};

/// Mirrors `include/libdeluge/sample_reader.h`'s `DelugeReadHint`. Crosses the FFI by value (see
/// `abi::deluge_sample_reader_open`), so `#[repr(u8)]` (fixed width) exactly matches the header's
/// explicit `: uint8_t` — the same fixed-width need `deluge_sample_source::abi::DelugeRegionState`
/// documents for its own enum.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ReadHint {
    /// Share the source residency cache — warm clusters the voice already holds are free hits.
    Cached = 0,
    /// One-shot whole-sample scan (waveform pre-scan, wavetable build): do NOT evict/pollute the
    /// voice's warm working set for a read that will never repeat.
    Scan = 1,
}

/// Mirrors `include/libdeluge/sample_source.h`'s `DelugeSampleGeometry` field-for-field, exactly
/// like `deluge_sample_source::geometry::Geometry` does for the voice port — a SEPARATE local
/// mirror rather than a shared dependency on that crate, so the non-voice reader stays decoupled
/// from the voice cursor. Each side of the port re-mirrors the one C struct it needs; this is the
/// established convention in this workspace (compare `deluge_sample_fill::FillContext`'s own
/// independent mirror of its header).
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

/// "Still recording, length unknown" sentinel (`sample_recorder.cpp`), identical to
/// `deluge_sample_source::geometry::UNKNOWN_LENGTH_SENTINEL` and `deluge_sample_fill::fill_logic`'s
/// own copy — kept as its own literal here too rather than importing either crate, matching this
/// workspace's established "each side re-mirrors the one constant it needs" convention (see this
/// module's own `Geometry` doc). `pub(crate)`, not private: `abi::tests`'s own `invalidate` sentinel
/// test needs the exact same value `invalidate` itself guards on, rather than a second copy of the
/// literal drifting from this one — intra-crate sharing, not the cross-crate case the doc above
/// deliberately avoids.
pub(crate) const UNKNOWN_LENGTH_SENTINEL: u64 = 0x8FFF_FFFF_FFFF_FFFF;

/// Valid payload bytes for cluster `index` of `geo`'s audio-data byte-stream — mirrors
/// `deluge_sample_source::geometry::resident_bytes_for`, NOT imported: this crate deliberately
/// stays independent of `deluge_sample_source` (see this crate's Cargo.toml doc), so the
/// short-last-cluster arithmetic
/// is re-derived here rather than shared. Full cluster size for a full cluster, the remainder for a
/// short last cluster, `0` for a cluster wholly past the end, and full cluster under the
/// unknown-length sentinel / zero length (still-recording samples, or a geometry not yet finalized).
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

/// Map a registered per-asset [`deluge_sample_fill::FillContext`] onto this crate's own
/// `Geometry` mirror, field-for-field — shared by [`Reader::open`] and [`peek`] so the two
/// entry points resolve geometry identically rather than each re-deriving the mapping.
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

/// One resolved frame position: which cluster it falls in, its byte offset within that cluster,
/// and how many bytes of that cluster are valid audio data (`resident_bytes_for`). Mirrors
/// `sample.cpp`'s own frame->cluster mapping (`getPlayByteLowLevel`/the perc-cache scan, both
/// derived from `sourceBytePos = audioDataStartPosBytes + startPosSamples * bytesPerSample`,
/// `sourceClusterIndex = sourceBytePos >> Cluster::size_magnitude`, `bytePosWithinCluster =
/// sourceBytePos & (Cluster::size - 1)`): `geo.cluster_size_bytes` is always a real `Cluster::size`
/// (a power of two), so plain `/`/`%` reproduce that shift/mask exactly, without needing a
/// `cluster_size_magnitude` field on this crate's own `Geometry` mirror. `None` only for a
/// malformed geometry (zero frame stride or zero cluster size) — never for an ordinary past-the-end
/// position, which instead resolves normally with `resident == 0` (the caller's EOF signal).
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
    /// The single process-wide resource-manager singleton (`include/libdeluge/streaming_fill.h`).
    /// Resolved once at `open()` time and cached — the same boot-singleton contract
    /// `deluge_sample_source::manager_residency::ManagerResidency::new` requires of its own handle.
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// Enqueue a reserved chunk's fill on the async drain and return at once
    /// (`include/libdeluge/streaming_fill.h`). The chunk is enqueued under a queue-owned lease, so the
    /// fill survives this reader dropping its own; the caller's next tick takes a cache hit. Real body
    /// in `streaming_loader.rs`.
    pub fn deluge_streaming_request_chunk(chunk_backing: *mut c_void);
}

/// A reader's frame-cursor state over one sample's source residency
/// (`include/libdeluge/sample_reader.h`'s `DelugeSampleReader`).
pub struct Reader {
    asset: u32,
    geometry: Geometry,
    direction: i8,
    hint: ReadHint,
    current_frame: u64,
    /// Whether the MOST RECENT `window()` succeeded: `false` if the last `window()` hit a
    /// malformed-geometry or acquire-or-fill failure (a transient card error / OOM is retried on the
    /// next `window()` — the flag is no longer sticky). Distinct from EOF (`frame_count == 0` with
    /// `ok` still `true`) — see `deluge_sample_reader_ok`'s header doc. A reader opened over an asset
    /// with no registered fill-context has zeroed geometry, so every `window()` re-fails and `ok`
    /// stays effectively `false`.
    ok: bool,
    /// The process-wide resource-manager handle, resolved once at `open()` (see the
    /// `deluge_streaming_resource_manager` extern above).
    manager: *mut c_void,
    // `pub(crate)`, not private: `abi.rs`'s own tests reach through the opaque pointer to attach a
    // real lease directly, since some tests need to populate this field outside the public API.
    // Never exposed outside this crate.
    pub(crate) held_lease: Option<Lease>,
    /// The cluster index `held_lease` is pinned to, if any — `window()`'s self-pin bookkeeping:
    /// `advance()` drops the held lease exactly when the cursor's new position leaves this index.
    held_cluster: Option<u32>,
}

impl Reader {
    /// Build a reader over `asset` — the resource-manager Asset id ALREADY defined for this sample
    /// (the header's `source_id`; resolved by the caller via `deluge_streaming_define_asset`, the
    /// SAME asset the voice port's own residency uses — see the header's doc for `source_id`, and
    /// `chunk_residency.cpp`'s `deluge_streaming_define_asset` for how the C++ side obtains it).
    ///
    /// Positioned at `start_frame`, reading in `direction` (+1 forward, -1 reverse), with `hint`
    /// steering eviction pressure (see [`ReadHint`]). Pure state construction plus one table
    /// lookup: no I/O, no lease taken yet.
    ///
    /// # Geometry resolution
    /// The header's `open()` signature takes only `source_id` — deliberately, per the design doc:
    /// geometry is resolved RUST-SIDE from the asset, not passed by the caller. This looks up
    /// `asset`'s per-asset fill-context (`deluge_sample_fill::fill_context_for`, the SAME table
    /// `deluge_streaming_set_fill_context` registers once at sample-load and the native fill
    /// already reads) and maps its fields onto this reader's own `Geometry` mirror. If `asset` has
    /// no registered context (a reader opened before the sample's first stream use, or over a bare
    /// asset id with nothing registered), `geometry` stays zeroed and the reader is marked
    /// not-ok — it cannot map frames without real geometry, and `window()` must not pretend
    /// otherwise.
    pub fn open(asset: u32, start_frame: u64, direction: i8, hint: ReadHint) -> Self {
        // SAFETY: returns the one process-wide resource-manager singleton; a stable pointer, no
        // aliasing/ownership concern (mirrors deluge_sample_fill::native's own call).
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

    /// Random-access reposition (for the both-directions hop search): release any
    /// held pin — a later `window()` call re-pins at the new position — and move the cursor to
    /// `frame`. Like `open()` without re-allocating a reader.
    pub fn seek(&mut self, frame: u64) {
        self.held_lease = None; // Option::drop releases the lease, if any, exactly once.
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

    /// See `deluge_sample_reader_ok`'s header doc.
    pub fn ok(&self) -> bool {
        self.ok
    }

    /// Acquire cluster `cluster_index`, composed from the facade's own primitives: a cache hit
    /// (`acquire_leased`) returns immediately; a miss `request`s a fresh reservation
    /// (construct-only, mirroring the async prefetch path's OWN asset registration) and hands its
    /// fill to the async drain. `Some` only once the chunk is
    /// ready -- which a drain that answered before `deluge_streaming_request_chunk` returned makes
    /// true at once; otherwise `None`, and the caller retries on its next tick, which takes a cache
    /// hit. `None` also on a reservation failure (the reservation's lease is dropped either way, so
    /// nothing leaks).
    ///
    /// # `DELUGE_READ_SCAN`'s hint
    /// The design calls for `DELUGE_READ_SCAN` to pass the manager a low-value/no-retain hint so a
    /// one-shot scan doesn't win eviction over the voice's warm working set. Checked against the
    /// facade/manager surface this crate can reach (`acquire_leased`/`request`/`try_acquire`/`peek`/
    /// `loader_enqueue`): none of them takes a per-acquire eviction-value or recency-bypass
    /// parameter today — eviction rank is purely a function of the ASSET's registered cost, size,
    /// and recency (`deluge_resource::value::evict_rank`), and `loader_enqueue`'s own `priority`
    /// only orders the async fill queue. So `hint` is stored on `Reader` and forwarded through the
    /// public API, but has no manager lever to act on yet — an honest, derived-not-invented gap, not silently dropped.
    fn acquire_and_fill(&self, cluster_index: u32) -> Option<Lease> {
        if self.manager.is_null() {
            // No manager yet (e.g. boot ordering) -- nothing to acquire through. Mirrors
            // `deluge_sample_source_open`'s own null-manager guard.
            return None;
        }
        // SAFETY: `self.manager` is non-null (checked above) and, per the boot-singleton contract
        // `deluge_streaming_resource_manager` documents, live for the process's remaining life.
        let resource = unsafe { Resource::from_handle(self.manager as *mut DelugeResource) };
        if let Some(lease) = resource.acquire_leased(self.asset, cluster_index) {
            return Some(lease);
        }
        let lease = resource.request(
            self.asset,
            cluster_index,
            self.geometry.cluster_size_bytes as usize,
        )?;
        let backing = lease.chunk().as_ptr() as *mut c_void;
        // Request the fill on the async drain and never wait for it: a synchronous `block_on` here
        // would deadlock a single-threaded executor against the drain it waits for. The caller (the
        // display-only background waveform overview pre-scan, or a pitch scan) retries on its next
        // tick, which takes a cache hit -- never affecting audio/stem output (overview data is
        // display-only).
        // SAFETY: `deluge_streaming_request_chunk` is the streaming-fill C-ABI; `backing` is the
        // just-`request`ed, still-leased chunk (its lease is held here).
        unsafe { deluge_streaming_request_chunk(backing) };
        if !resource.is_ready(lease.chunk()) {
            return None; // `lease` drops here; the queue's own lease keeps the fill alive.
        }
        Some(lease)
    }

    /// Transiently `acquire_and_fill` cluster `index`, dropping the lease immediately (never
    /// self-pinned — only [`Self::window`]'s OWN cluster is). See `window`'s "boundary straddle"
    /// doc for why this call exists: it exists purely to trigger `index`'s own `native_finish`
    /// (on a miss) so that, if the currently-self-pinned cluster is its immediate predecessor, the
    /// UNCONDITIONAL "give extra bytes to the previous cluster" step in `stitch_prev`
    /// (`stitch.cpp:15-23`, mirrored by `deluge_sample_convert::stitch_boundaries`) copies `index`'s
    /// own head bytes into that predecessor's trailing slack. `true` iff `index` ended up resident
    /// (cache hit or a successful fresh fill) — this does NOT independently confirm the stitch
    /// happened (see `window`'s doc for the residual gap when `index` was already resident from an
    /// unrelated earlier fill).
    fn ensure_neighbour_resident(&self, index: u32) -> bool {
        self.acquire_and_fill(index).is_some()
    }

    /// The contiguous run of valid, already-converted frames at the cursor. See the header doc for
    /// `deluge_sample_reader_window`'s full contract.
    ///
    /// Maps `current_frame` to its `(cluster_index, byte_offset, resident_bytes)` via [`locate`];
    /// `byte_offset >= resident_bytes` is unambiguous end-of-audio (`{null, 0}`, `ok` untouched —
    /// the ordinary, expected way a scan terminates, not a failure) — there is no more real audio
    /// data starting here, in either direction.
    ///
    /// # Boundary straddle: a frame's bytes can span two clusters
    /// `frame_stride` (`byte_depth * num_channels`) does not generally divide `cluster_size_bytes`
    /// evenly — the common real case: 24-bit audio (`byte_depth == 3`, set by `sample_recorder.cpp`)
    /// against a power-of-two cluster size. So the LAST frame of a non-final cluster can have bytes
    /// past `cluster_size_bytes`, in the next cluster. The current C++ read path serves exactly this
    /// through each chunk's `payload_with_trailing_slack()` span (`cluster_size_bytes + 7` bytes,
    /// `cluster.h`) — `native_finish`'s `stitch_boundaries` call (`stitch.cpp`) keeps that trailing
    /// slack in sync with the true next-cluster bytes via TWO complementary, unconditional (i.e.
    /// format-independent) copies: `stitch_next` (self's own finish, when the next cluster is
    /// ALREADY resident, copies the next cluster's own head into self's trailing slack directly —
    /// `stitch.cpp:145-148`'s `need_copy7` "NATIVE" fallthrough) and `stitch_prev` (the mirror: the
    /// LATER-filled neighbour's own finish copies ITS OWN head into the EARLIER cluster's trailing
    /// slack — `stitch.cpp:20-23`). Either one suffices, whichever cluster's `native_finish` runs
    /// while the other is already resident — so a boundary between two resident clusters is stitched
    /// regardless of fill order. Reproducing this: a window whose run includes a straddling frame
    /// [`Self::ensure_neighbour_resident`]s the next cluster FIRST (after this cluster is already
    /// self-pinned resident, so whichever of the two finish calls runs next performs the copy),
    /// reading the frame's overflow bytes from the (now-stitched) trailing slack — max overhang is
    /// `frame_stride - 1` bytes, always < 7 for every real `byte_depth`/`num_channels` combination,
    /// so it never reaches past the slack's own end.
    ///
    /// This ONLY applies when the straddling frame is REAL audio — i.e. the current cluster is FULL
    /// (`resident_bytes == cluster_size_bytes`) and the next cluster actually has more data
    /// (`resident_bytes_for(cluster_index + 1) > 0`); otherwise the partial tail bytes are past the
    /// true end of the audio (a short/last cluster) and are dropped, not read. Frames strictly
    /// BEFORE a straddling one never need this (their own bytes stay within this cluster), and
    /// backward reads only ever need it for the single frame AT the cursor (every earlier frame,
    /// walking back toward byte 0, moves further from the boundary) — there is no equivalent
    /// "leading slack" for a frame straddling INTO a cluster from its predecessor (`frame_read_
    /// origin`'s own front guard is explicitly don't-care bytes, `cluster.h`), so a backward window
    /// never needs one.
    ///
    /// If [`Self::ensure_neighbour_resident`] fails (the neighbour can't be made resident), the
    /// straddling frame is dropped rather than failing the whole reader — a transient
    /// neighbour-acquire failure doesn't invalidate the cluster this reader is already validly
    /// pinned to, and a later `window()` call may still succeed once the failure clears (e.g.
    /// eviction pressure eases). WHICH frame that is, and which end of the window it's dropped
    /// from, differs by direction — the straddling frame is always the one nearest the boundary,
    /// but "nearest the boundary" is the LAST frame in a forward run (`ptr`, unchanged, already
    /// excludes it once the count shrinks) and the FIRST frame — the one AT the cursor itself,
    /// where `ptr` points — in a backward run, so a backward degrade must retreat `ptr` one
    /// `frame_stride` earlier (never just shrink the count) or the caller would still be handed a
    /// pointer straight at the unconfirmed straddling bytes. If dropping it leaves zero servable
    /// frames (it was the only one), that is reported as a FAILURE (`ok = false`), not EOF — real
    /// audio exists here; it just couldn't be confirmed safe this call — so `frame_count == 0 &&
    /// !reader_ok()` (retry later) stays distinguishable from `frame_count == 0 && reader_ok()`
    /// (true end-of-audio), exactly as the header's `deluge_sample_reader_ok` contract promises.
    ///
    /// **Residual, derived (not invented) limitation**: if the next cluster was already resident
    /// from an EARLIER, unrelated fill (e.g. another reader/the voice warmed it before this cluster
    /// ever existed), `ensure_neighbour_resident`'s cache hit does not re-run `native_finish` and so
    /// cannot retroactively trigger the stitch. This is a property of the underlying stitch
    /// mechanism itself (the same one the current C++ consumers rely on), not a gap this reader
    /// introduces; the byte-identity differential (a later task) is the right place to prove this
    /// doesn't matter in practice for this reader's own (sequential) access pattern.
    ///
    /// Otherwise: acquires (and, on a miss, requests the fill of) the cluster via
    /// [`Self::acquire_and_fill`], replacing any prior held lease (ONE held pin — the self-pin
    /// invariant), and returns a pointer into it plus how many WHOLE (possibly straddling) frames
    /// are valid in the reader's own `direction` from here.
    ///
    /// An acquire/fill failure for THIS cluster, or a fill still in flight, sets `ok = false` and
    /// returns `{null, 0}` — distinct from the EOF case above (which leaves `ok` alone).
    pub fn window(&mut self) -> (*const u8, u32) {
        // Each window() is judged fresh: a prior transient failure (card error / OOM) must not latch
        // the reader dead. Every existing failure path below re-sets `ok = false` for THIS attempt,
        // so ok() still means "did the most recent window() succeed"; a permanent fault (malformed
        // geometry) simply re-fails its own cheap check next call.
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

        // Is a boundary straddle at this cluster's tail REAL, readable audio? Only true for a FULL
        // (non-last) cluster whose successor genuinely has more data — see this fn's own doc.
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
            // Backward: only the frame AT the cursor (byte_offset) can possibly straddle -- every
            // earlier one (byte_offset - k*frame_stride, k >= 1) is strictly further from the
            // cluster's own end, never straddling.
            if byte_offset + frame_stride > resident {
                if straddle_is_real_audio() {
                    (byte_offset / frame_stride + 1, true)
                } else {
                    // The frame at the cursor itself isn't fully valid audio -- EOF.
                    return (core::ptr::null(), 0);
                }
            } else {
                (byte_offset / frame_stride + 1, false)
            }
        };
        if frame_count == 0 {
            return (core::ptr::null(), 0); // No whole (or validly straddling) frame remains -- EOF.
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
            // `cluster_index + 1` cannot overflow here: `needs_next` is only ever set via
            // `straddle_is_real_audio`, which itself only returns `true` when its own
            // `checked_add(1)` succeeded.
            if !self.ensure_neighbour_resident(cluster_index + 1) {
                // Degrade: drop the unstitched straddling frame rather than fail the whole reader.
                frame_count -= 1;
                if frame_count == 0 {
                    // The straddling frame was the ONLY one -- real audio exists here, but
                    // couldn't be confirmed safe this call. A failure, not EOF (see this fn's
                    // own "boundary straddle" doc).
                    self.ok = false;
                    return (core::ptr::null(), 0);
                }
                if self.direction < 0 {
                    // Backward: the straddling frame is the one AT the cursor (byte_offset) --
                    // `ptr` points straight at it, so shrinking `frame_count` alone (forward's
                    // fix) does nothing here; retreat the window's start one `frame_stride`
                    // EARLIER (further from the boundary) so `ptr` lands on the next frame back,
                    // fully inside this cluster. Safe: `frame_count` (post-decrement) > 0 here
                    // implies the PRE-decrement count was > 1, i.e. `byte_offset / frame_stride
                    // >= 1`, i.e. `byte_offset >= frame_stride` -- no underflow.
                    window_byte_offset -= frame_stride;
                }
                // Forward: the straddling frame is the LAST index in the run; `ptr` (unchanged,
                // still at byte_offset) already excludes it once the count above shrank.
            }
        }

        // SAFETY: `held_lease` (just confirmed `Some` above) pins a resident chunk for as long as
        // this reader holds it; `deluge_sample_fill::chunk::payload` returns that chunk's payload
        // base.
        let payload_base = unsafe {
            deluge_sample_fill::chunk::payload(
                self.held_lease.as_ref().unwrap().chunk().as_ptr() as *mut c_void
            )
        };
        // SAFETY: the only frame that can ever extend past `resident` is the single one nearest
        // the boundary -- the LAST frame of a forward run, or the FIRST (the one AT
        // `window_byte_offset` itself) of a backward run -- and only when `needs_next` held AND
        // the neighbour was confirmed resident (the un-degraded path): that frame's last byte
        // lands at `resident + (frame_stride - 1 - remainder) <= cluster_size_bytes + frame_stride
        // - 2`, which stays inside the `cluster_size_bytes + 7`-byte `payload_with_trailing_slack()`
        // span for every real `frame_stride` (<= 8; see this fn's own doc). In the degraded case
        // that boundary-adjacent frame is excluded from the window entirely (forward: dropped off
        // the end; backward: `window_byte_offset` retreated past it above), so every byte this
        // window can ever expose without a confirmed stitch stays within `[0, resident)`.
        let frames = unsafe { payload_base.add(window_byte_offset as usize) };
        (frames as *const u8, frame_count)
    }

    /// Advance the cursor `frames` frames in this reader's own `direction`, saturating at 0 (never
    /// underflows) and at the sample's own total frame count (never runs away past the end on
    /// repeated forward advances). Crosses cluster boundaries internally: if the new position
    /// leaves the currently-held backing's cluster, the held lease is dropped (self-pin release) —
    /// the next `window()` re-pins at the new position.
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
                self.held_lease = None; // Option::drop releases the lease, if any.
                self.held_cluster = None;
            }
        }
    }

    /// The stateless copy-out convenience (`deluge_sample_read`, header doc): copy up to
    /// `num_frames` native-format frames of `asset`, starting at `start_frame`, into `dest`.
    /// Internally an `open` -> `window`/`advance` loop -> drop (ordinary `Drop`, the same "close"
    /// this crate's lifecycle tests exercise at the `Box`/`Drop` level — see
    /// `abi::deluge_sample_reader_close`'s own doc) -- ONE residency path, not a second
    /// implementation. Always forward (`direction ==
    /// 1`) with [`ReadHint::Cached`], matching the header's own "equivalent to driving the handle
    /// API by hand with `DELUGE_READ_CACHED`" contract.
    ///
    /// `stride` (the geometry's own `byte_depth * num_channels`) is resolved from `asset`'s
    /// geometry, exactly like [`Self::window`]'s own frame-stride math -- not passed by the caller.
    /// Each loop iteration copies `min(window's frame_count, frames still wanted, dest bytes still
    /// available / stride)` frames, so the copy is bounded by BOTH `num_frames` and `dest_bytes` on
    /// every iteration, not just the first. Stops at end-of-audio (`window`'s `frame_count == 0`),
    /// once `num_frames` frames are written, or once `dest_bytes` is exhausted (whichever comes
    /// first) -- returns the number of frames actually written, short in the first and last cases.
    ///
    /// A malformed/unresolved geometry (`stride == 0` -- e.g. `asset` has no registered
    /// fill-context, mirroring [`Self::open`]'s own not-ok case) returns `0` without ever calling
    /// `window()` -- there is no frame stride to divide `dest_bytes` by, and `window()` would
    /// report the same "nothing servable" outcome anyway (`ok() == false`) once reached.
    ///
    /// # Safety
    /// `dest` must be valid for at least `dest_bytes` writable bytes (this fn never writes past
    /// `dest_bytes`, but the caller must supply a real allocation of at least that size).
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
                break; // End-of-audio (or a hard failure -- either way, nothing more to copy).
            }
            let frames_remaining = num_frames - written_frames;
            let max_by_dest = ((dest_bytes - written_bytes) / stride) as u32;
            let n = frame_count.min(frames_remaining).min(max_by_dest);
            if n == 0 {
                break; // `dest_bytes` is exhausted -- less than one whole frame's room remains.
            }
            let copy_bytes = n as usize * stride;
            // SAFETY: `frames` points into `window`'s pinned, resident chunk, valid for at least
            // `frame_count * stride` bytes (`window`'s own contract) and `n <= frame_count`, so
            // `copy_bytes` stays within it; `dest.add(written_bytes)` plus `copy_bytes` stays
            // within `dest_bytes` by construction of `max_by_dest` above, and `dest` is valid for
            // `dest_bytes` bytes per this fn's own SAFETY contract.
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

/// Stateless, passive resident-peek (`deluge_sample_peek`, header doc): the resident,
/// already-converted frames at `start_frame` of `source_id`'s residency, as a zero-copy run to
/// the CONTAINING CLUSTER's own boundary in `direction`.
///
/// # Residency is the null pointer, not the count
/// The residency signal is the returned `frames` pointer, NOT `frame_count` -- this makes the
/// peek the exact equivalent of the C++ facade `peek` it replaces. `frames == null` iff there is
/// no valid resident position here: null manager, no registered geometry, malformed geometry,
/// `start_frame` past this cluster's real audio, not resident, or resident-but-not-yet-ready. A
/// NON-null `frames` is always a valid pointer into the resident cluster payload; `frame_count`
/// may then legitimately be `0`, meaning "resident and ready, but the cursor sits on the last
/// PARTIAL frame -- fewer than one whole frame fits in `direction`." The caller reads that frame
/// from the pointer with its own byte-math (as `Sample::getAveragesForCrossfade` does), so a `0`
/// count must NOT be mistaken for "not resident."
///
/// Reuses the SAME geometry resolution and frame->cluster mapping [`Reader::open`]/[`locate`]
/// use, and the SAME `deluge_sample_fill::chunk::payload` accessor [`Reader::window`] uses -- no
/// separate geometry or payload logic. Unlike `window`/`advance` this has no cursor to hold: no
/// lease is ever taken, no recency is ever bumped (`Resource::peek`'s own contract), no load is
/// ever triggered on a miss, and no neighbouring cluster is ever touched -- render-thread-safe.
///
/// # Within-cluster only (checkpoint-review I1)
/// This deliberately does NOT reuse [`Reader::window`]'s cross-cluster stitched-slack straddle
/// serve. That serve is proven safe only for a SEQUENTIAL reader's own access pattern (see
/// `window`'s doc): it self-pins the current cluster and then transiently resident-fills the
/// NEXT one purely to trigger its `native_finish`'s boundary stitch. A peek call is random-access
/// and takes no pin at all, so it has no business ever touching a second cluster; the run this
/// function returns always stays within the single cluster `start_frame` maps into.
///
/// # The backward-window pointer convention (checkpoint-review I2)
/// `frames` always points AT `start_frame` itself, in both directions. For `direction == +1`
/// `frame_count` extends FORWARD from there (toward higher addresses, up to the cluster's own
/// last resident frame); for `-1` it extends BACKWARD (toward lower addresses, down to the
/// cluster's own first frame) -- the caller walks down from `frames` in that case, exactly like
/// `window`'s own backward run.
pub fn peek(source_id: u32, start_frame: u64, direction: i8) -> (*const u8, u32) {
    // SAFETY: the one process-wide resource-manager singleton -- same call, same contract as
    // `Reader::open`'s own use of this extern.
    let manager = unsafe { deluge_streaming_resource_manager() };
    if manager.is_null() {
        return (core::ptr::null(), 0);
    }
    let Some(ctx) = deluge_sample_fill::fill_context_for(source_id) else {
        return (core::ptr::null(), 0); // No registered geometry -- nothing to map frames against.
    };
    let geometry = geometry_from_context(ctx);
    let Some((cluster_index, byte_offset, resident)) = locate(start_frame, &geometry) else {
        return (core::ptr::null(), 0); // Malformed geometry (zero stride or zero cluster size).
    };
    if byte_offset >= resident {
        return (core::ptr::null(), 0); // Past the end of this cluster's real audio data.
    }
    // `locate`'s own `None` case above already rules out a zero stride, so this can't be 0.
    let frame_stride = geometry.byte_depth as u32 * geometry.num_channels as u32;

    // SAFETY: `manager` is non-null (checked above) and, per the boot-singleton contract
    // `deluge_streaming_resource_manager` documents, live for the process's remaining life.
    let resource = unsafe { Resource::from_handle(manager as *mut DelugeResource) };
    let Some(chunk) = resource.peek(source_id, cluster_index) else {
        return (core::ptr::null(), 0); // Not resident -- a true miss, not this fn's job to fill.
    };
    if !resource.is_ready(chunk) {
        return (core::ptr::null(), 0); // Resident but not yet loaded -- the ready gate.
    }

    // No straddle rounding here (see this fn's own "within-cluster only" doc): a forward run
    // takes only WHOLE frames up to `resident`; a backward run takes every whole frame from
    // `start_frame` back to the cluster's own first frame (byte_offset / frame_stride whole
    // frames below it, plus the one at `byte_offset` itself).
    let frame_count = if direction >= 0 {
        (resident - byte_offset) / frame_stride
    } else {
        byte_offset / frame_stride + 1
    };
    // No early return on `frame_count == 0`: residency is signalled by the non-null `frames`
    // pointer, NOT the count (see this fn's doc). A resident, ready cluster whose cursor sits on
    // its last PARTIAL frame (`resident - byte_offset < frame_stride`, forward) has zero WHOLE
    // frames ahead yet is a perfectly valid resident position -- the caller reads that partial
    // frame from `frames` with its own byte bounds, exactly as the facade `peek` it replaces does.

    // SAFETY: `chunk` was just confirmed resident (and ready) by `resource.peek`/`is_ready`
    // above; `deluge_sample_fill::chunk::payload` returns that chunk's payload base, valid for as
    // long as it stays resident -- guaranteed for the remainder of this call (a peek never
    // triggers eviction: it takes no lease, but it also never blocks or yields).
    let payload_base = unsafe { deluge_sample_fill::chunk::payload(chunk.as_ptr() as *mut c_void) };
    // SAFETY: within-cluster only, by construction: the forward run's last byte is at
    // `byte_offset + frame_count * frame_stride <= resident <= cluster_size_bytes`, and the
    // backward run never reads past `byte_offset` itself (`resident <= cluster_size_bytes`) --
    // every byte this fn can ever expose stays inside the plain, unpadded cluster payload; the
    // trailing-slack straddle span `window()` sometimes reads is never touched here.
    let frames = unsafe { payload_base.add(byte_offset as usize) };
    (frames as *const u8, frame_count)
}

/// Invalidate every currently-resident cluster of `source_id`'s sample — the Rust backing of the
/// header's `deluge_sample_invalidate`. For each resident cluster: flag it unloadable (so a mid-flight
/// async fill won't complete with stale bytes) and remove it from the load queue (cancel a pending
/// load). Faithful to C++ `Sample::markAsUnloadable`'s `for c in 0..num_clusters()` peek-loop, with
/// the cluster span resolved Rust-side from the fill context (geometry is Rust-owned; no count is
/// threaded from C++). A sample with no registered fill-context, or none of whose clusters are
/// resident, invalidates nothing (a no-op) — matching a peek-loop that finds every `peek` null.
pub fn invalidate(source_id: u32) {
    // SAFETY: the process-wide boot-singleton, same contract `open`/`peek` rely on.
    let manager = unsafe { deluge_streaming_resource_manager() };
    if manager.is_null() {
        return;
    }
    let Some(ctx) = deluge_sample_fill::fill_context_for(source_id) else {
        return; // No registered geometry -> nothing resident through this port.
    };
    if ctx.cluster_size == 0 || ctx.cluster_size_magnitude >= 64 {
        return; // Malformed geometry -- mirrors resolve_geometry's own guard.
    }
    // C++ `Sample::num_clusters()` branches on `isLengthKnown()`: `geometricClusterCount()` for a
    // known length, `liveRecorderClusterCount()` (tracked by the recorder itself, not this fill
    // context) while still recording. `audio_data_length_bytes == UNKNOWN_LENGTH_SENTINEL` is
    // exactly that "still recording" case (same sentinel `resident_bytes_for` and `advance` already
    // guard on above); `== 0` is the same "no known length yet" shape. Neither has a finite
    // geometric bound available here, and the live recorder-side count is out of scope for this
    // module -- so both bail out as a no-op rather than feeding an astronomical `total_bytes`
    // into the geometric formula below (which `.min(u32::MAX)` would otherwise clamp to ~4.3 billion,
    // turning the peek-loop below into an effective hang).
    if ctx.audio_data_length_bytes == 0 || ctx.audio_data_length_bytes == UNKNOWN_LENGTH_SENTINEL {
        return;
    }
    // The sample's whole cluster span [0, num_clusters): faithful to markAsUnloadable's own
    // `0..num_clusters()`. For this (now-confirmed known-length) sample, num_clusters() ==
    // geometricClusterCount() == ceil((start + length) / cluster_size) exactly. Iterating from 0
    // (not first_with_data) is deliberate: a pre-audio header cluster can be resident under this
    // asset from the load-time parse, and markAsUnloadable invalidates it too.
    let total_bytes = ctx.audio_data_start_pos_bytes as u64 + ctx.audio_data_length_bytes;
    let num_clusters = total_bytes
        .div_ceil(ctx.cluster_size as u64)
        .min(u32::MAX as u64) as u32;
    // SAFETY: non-null manager, boot-singleton lifetime (see walk_and_lease's own use of this).
    let resource = unsafe { Resource::from_handle(manager as *mut DelugeResource) };
    for index in 0..num_clusters {
        if let Some(chunk) = resource.peek(source_id, index) {
            // SAFETY: `chunk` is a live resident backing from `peek`; the setter only writes the
            // POD's `unloadable` byte.
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

    /// `construct` seeds a per-index ramp — the same tiny fixture the sibling region-port crates
    /// use (`cursor.rs`/`manager_residency.rs` in `deluge_sample_source`); nothing here reads the
    /// bytes back (these lifecycle tests never call `window()`), it just needs SOME construct
    /// callback attached so `Resource::request` can reserve a chunk to mint a real `Lease`.
    unsafe extern "C" fn make_ramp_construct(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        index: u32,
        dest: *mut u8,
    ) {
        for b in 0..CHUNK_SIZE {
            // SAFETY: `dest` is the manager's just-allocated `CHUNK_SIZE`-byte backing for this
            // chunk (per `ConstructFn`'s contract).
            unsafe {
                *dest.add(b) = index.wrapping_add(b as u32) as u8;
            }
        }
    }

    /// Backing arena for the leaked test heap, kept alive for the process's remaining life —
    /// mirrors the sibling crates' own boot-singleton test-harness contract.
    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// A `FillContext` compatible with `CHUNK_SIZE` (4096 = 2^12) — just enough for `Reader::open`
    /// to resolve real geometry (`ok() == true`); these lifecycle tests never call
    /// `window()`, so the exact values beyond `cluster_size`/`cluster_size_magnitude` don't matter.
    fn lifecycle_fill_context() -> FillContext {
        FillContext {
            efatfs_handle: 0,
            audio_data_start_pos_bytes: 0,
            audio_data_length_bytes: 0, // unknown/irrelevant here -> resident_bytes_for treats as unbounded
            first_cluster_index_with_no_audio_data: -1,
            cluster_size: CHUNK_SIZE as u32,
            cluster_size_magnitude: 12, // 2^12 == 4096 == CHUNK_SIZE
            raw_data_format: 0,
            byte_depth: 2,
            num_channels: 1,
        }
    }

    /// Build a manager over a fresh test heap (leaking its backing arena) with one requestable
    /// asset attached, mirroring `deluge_sample_source`'s own test harness. Also registers the
    /// asset's fill-context (`Reader::open` resolves geometry from this table) and routes this
    /// thread's `deluge_streaming_resource_manager()` stub to `handle`
    /// — every caller must hold [`TEST_LOCK`] for its whole run, since both the fill-context table
    /// and the stub's active-manager cell are shared, process-wide state (see their own docs).
    fn test_manager_and_asset() -> (*mut DelugeResource, u32) {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf: Vec<u128> = std::vec![0u128; words];
        let ptr = buf.as_mut_ptr() as *mut u8;
        // SAFETY: `buf` is leaked below so the arena stays alive for the whole test binary's
        // life, matching the sibling crates' own boot-singleton test-harness contract.
        let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
        std::mem::forget(TestHeap { _buf: buf });
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        // SAFETY: `handle` is live; `make_ramp_construct` has the required C-ABI signature.
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

    /// The central lifecycle assertion: `seek` both updates `current_frame` AND drops
    /// whatever lease the reader is holding — proven with a REAL manager lease (not a stand-in),
    /// attached directly to `held_lease` (this `mod` is a child of `reader`'s own module),
    /// mirroring how `window()` itself populates it.
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

    /// `seek` with nothing held is a plain reposition — no lease to drop, no panic.
    #[test]
    fn seek_with_no_held_lease_just_moves_the_cursor() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let (_handle, asset) = test_manager_and_asset();
        let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
        reader.seek(7);
        assert_eq!(reader.current_frame(), 7);
    }

    /// Close (the C-ABI wrapper just drops the owning `Box<Reader>` — see `abi.rs`)
    /// releases any held lease without leaking, proven here at the `Box`/`Drop` level: a boxed
    /// reader (mirroring what `deluge_sample_reader_open` hands out)
    /// is non-null, and dropping it (mirroring `deluge_sample_reader_close`) returns the manager's
    /// lease count to its pre-open baseline.
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

        // SAFETY: `ptr` was just produced by `Box::into_raw` above and has not been freed yet.
        unsafe { drop(Box::from_raw(ptr)) }; // mirrors deluge_sample_reader_close

        assert_eq!(
            resource.lease_count_by_slot(slot),
            0,
            "close must release the held lease, with no leak"
        );
    }

    /// `resident_bytes_for`'s own mirror-check: identical shape to
    /// `deluge_sample_source::geometry::resident_bytes_for`'s own unit test, proving this crate's
    /// independent re-derivation (see this fn's doc for why it isn't shared) matches.
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
        let g = geo(68); // 4*16 + 4 -> clusters 0..3 full, cluster 4 has 4 bytes
        assert_eq!(resident_bytes_for(0, &g), 16);
        assert_eq!(resident_bytes_for(3, &g), 16);
        assert_eq!(resident_bytes_for(4, &g), 4);
        assert_eq!(resident_bytes_for(5, &g), 0);
        let s = geo(UNKNOWN_LENGTH_SENTINEL);
        assert_eq!(resident_bytes_for(99, &s), 16);
        let z = geo(0);
        assert_eq!(resident_bytes_for(0, &z), 16);
    }

    // ── window/advance over a real synthetic fill (the read core + self-pin) ───────────
    //
    // A synthetic sample: cluster_size=512 bytes, byte_depth=2/num_channels=1 (stride=2, so a
    // 512-byte cluster holds exactly 256 frames), RawDataFormat::Native (0) so
    // `deluge_sample_fill::native_finish`'s convert/stitch tail never rewrites `self`'s own
    // payload (see the module doc's derivation) — the fill stub's deterministic ramp
    // (`byte_offset + i`, `lib.rs`'s `host_streaming_stubs::deluge_efatfs_read_at`) survives to
    // `window()` untouched, so a test can assert on it directly. Real `deluge_resource` manager,
    // real `deluge_sample_fill::{native_begin, native_finish}`, real chunks — every backing this
    // module's `harness` mints is a genuine `StreamedChunk`, placement-constructed by the same
    // `deluge_sample_fill::chunk::deluge_streaming_chunk_construct` callback production registers
    // (`chunk_residency.cpp`), not a seeded `payload == backing` stand-in (`RUST_CHUNK_PAYLOAD_OFFSET`
    // is a real nonzero front guard). This is
    // also load-bearing for soundness, not just fidelity: `native_finish` (`native.rs`) reaches a
    // chunk's fields via `crate::chunk::payload`/`convert_state`/`set_convert_state`/`set_loaded`
    // directly (in-crate calls, not C-ABI wrappers) — those reborrow the backing as
    // `&`/`&mut StreamedChunk`, which is only valid over a backing this module actually
    // constructed as one.
    mod window_tests {
        use super::*;
        use crate::host_streaming_stubs::{set_fail_at_byte_offset, set_force_read_failure};

        // 512, not a smaller power of two: `fill_logic::begin`'s sector math is
        // `cluster_size >> 9` (512-byte sectors, mirroring the real `deluge_efatfs_read_at`
        // granularity) — a cluster smaller than one sector would report `num_sectors == 0` and
        // the fill would never read anything. Real `Cluster::size` is always far larger (a real
        // FAT cluster, KBs); 512 is the smallest size that keeps this synthetic sample honest.
        const CLUSTER_SIZE: u32 = 512;
        const CLUSTER_MAGNITUDE: u32 = 9; // 2^9 == 512
        /// Real per-chunk backing needs `RUST_CHUNK_PAYLOAD_OFFSET` (front guard) plus
        /// `CLUSTER_SIZE` (payload) plus 7 more bytes of trailing slack that `native_finish`
        /// always touches (see its own `payload_len` doc), rounded up to a 16-byte multiple for
        /// the slab, mirroring the real `kChunkPayloadOffset`-sized slab slots production
        /// configures for `BACKING_SLAB` assets (`deluge_alloc::slab`'s own 16-byte-aligned-slot
        /// contract — see `deluge_resource::lib`'s `slab_backed_asset_acquires_evicts_reloads`
        /// test). Derived from the real offset (not a hand-rounded literal) so this harness never
        /// silently drifts out of sync with `StreamedChunk`'s own layout.
        const BACKING_SIZE: usize =
            (deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET + CLUSTER_SIZE as usize + 7)
                .next_multiple_of(16);

        /// Placement-construct a real `StreamedChunk` at `dest` — the exact callback production
        /// registers for streamed-chunk assets (`chunk_residency.cpp`), reached through a thin
        /// `ConstructFn`-shaped forwarder (`deluge_resource::manager::ConstructFn` takes `dest: *mut
        /// u8`; the real construct's C-ABI signature takes `dest: *mut c_void` — the two function
        /// pointer types don't unify, so this wrapper is the cast, not a behavioural stand-in).
        ///
        /// # Safety
        /// `dest` must be a writable slab slot of at least `RUST_CHUNK_PAYLOAD_OFFSET +
        /// CLUSTER_SIZE + 7` bytes — this module's own `BACKING_SIZE` slab satisfies that.
        unsafe extern "C" fn real_chunk_construct(
            ctx: *mut c_void,
            owner: *mut c_void,
            index: u32,
            dest: *mut u8,
        ) {
            // SAFETY: forwarding the caller's contract (above) to the real construct.
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

        /// Build a manager+asset over a `BACKING_SIZE`-slot-size slab (mirroring production's own
        /// `DELUGE_RESOURCE_BACKING_SLAB` streaming-cluster assets — `chunk_residency.cpp`'s
        /// `deluge_streaming_define_asset` — rather than `BACKING_HEAP`: a slab's per-slot size is
        /// fixed at slab-creation time and the `size` argument `Reader::acquire_and_fill` passes to
        /// `request` is ignored, exactly like the real `kSlabBackedSizeIgnored` convention documents
        /// (`storage/cluster/cluster.h`)), register `ctx` as the asset's fill-context, and route
        /// the stub's active manager — see `test_manager_and_asset` (the parent module) for why
        /// every caller must hold [`TEST_LOCK`] for its whole run.
        fn harness(ctx: FillContext) -> (*mut DelugeResource, u32) {
            let words = (256 * 1024usize).div_ceil(16);
            let mut buf: Vec<u128> = std::vec![0u128; words];
            let ptr = buf.as_mut_ptr() as *mut u8;
            // SAFETY: leaked below, alive for the test binary's whole life.
            let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
            std::mem::forget(TestHeap { _buf: buf });
            // SAFETY: `h` is the live heap handle just created above; `BACKING_SIZE`/16 chunk slots
            // is ample for this module's own tests (at most 2 clusters resident at once).
            let slab =
                unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, BACKING_SIZE, 16) };
            assert!(!slab.is_null());
            // SAFETY: `h` is the live heap handle just created above.
            let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
            assert!(!handle.is_null());
            // SAFETY: `handle`/`slab` are both live, over the same heap.
            unsafe { deluge_resource::deluge_resource_set_slab(handle, slab) };
            // SAFETY: `handle` is live; `real_chunk_construct` has the required C-ABI signature.
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

        /// The synthetic fill's expected post-fill bytes for cluster `index`: the
        /// `deluge_efatfs_read_at` stub's ramp, `(index * CLUSTER_SIZE + i) as u8`.
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

            // SAFETY: `ptr` is a pinned, resident chunk's payload for as long as `reader`'s
            // `held_lease` (dropped only at the end of this scope) is alive.
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
            // Frame 5 -> abs byte pos 10 -> exactly the end of the real audio data.
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

            // Advance to exactly cluster 1's first frame (frame 5 + 251 = frame 256 -> byte 512 ->
            // cluster 1, offset 0): the self-pin invariant means the OLD pin is released as soon as
            // the cursor leaves it, even before the next window() call re-pins.
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

            // The card recovers: the NEXT window() must retry, not stay latched dead.
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

        // ── The boundary-straddle fix ────────────────────────────────────────────────────────
        //
        // 24-bit mono (`byte_depth: 3, num_channels: 1` -> `frame_stride == 3`) does NOT divide
        // `CLUSTER_SIZE` (512 % 3 == 2) -- the real case `sample_recorder.cpp` sets (`byteDepth =
        // 3`). Frame 170 (absolute bytes [510, 513)) straddles cluster 0/1's boundary: bytes
        // 510-511 are cluster 0's own payload, byte 512 only exists (correctly) after
        // `native_finish`'s stitch (`stitch.cpp`) copies cluster 1's own head into cluster 0's
        // trailing slack. Cluster 1's head (absolute file bytes 512..519, the synthetic fill's
        // ramp) is `[(512 + i) as u8 for i in 0..7]` == `[0, 1, 2, 3, 4, 5, 6]` (512 wraps to 0
        // mod 256) -- so the straddling frame's expected bytes are `[510, 511, 0]`.

        /// A 24-bit mono geometry: `byte_depth: 3, num_channels: 1` -> `frame_stride == 3`, which
        /// does not divide `CLUSTER_SIZE` -- see the section doc above.
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
            // 3 full clusters (1536 bytes): cluster 1 genuinely has more data past cluster 0's
            // tail, so the straddle at cluster 0's end is real, readable audio.
            let (_handle, asset) = harness(geo24(3 * CLUSTER_SIZE as u64));

            // Frame 170 -> abs byte pos 510 -> cluster 0, offset 510: bytes_available =
            // 512-510 = 2 < stride(3) -- a real straddle, not a whole-frame-aligned boundary.
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

            // SAFETY: `ptr` is cluster 0's pinned `payload_with_trailing_slack()`, valid for
            // these 3 bytes (byte_offset 510, 511 in the main payload; 512 in the trailing slack,
            // per `window`'s own safety derivation).
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

            // Same straddling frame (170), approached backward: the cursor's OWN frame is the
            // one that straddles -- every earlier frame (walking back toward byte 0) does not.
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

            // Same pointer position as the forward case (the cursor's OWN frame) -- same bytes.
            // SAFETY: see the forward test above.
            let got = unsafe { [*ptr, *ptr.add(1), *ptr.add(2)] };
            assert_eq!(got, [254u8, 255u8, 0u8]);
        }

        #[test]
        fn window_true_eof_with_24bit_stride_does_not_falsely_straddle() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            // 511 bytes of real audio: cluster 0 is genuinely short (resident 511 < 512), so its
            // own tail is NOT a real straddle -- there is no cluster 1 with more data to pull.
            let (_handle, asset) = harness(geo24(511));

            // Frame 170 -> abs byte pos 510 -> byte_offset 510, resident 511: only 1 byte
            // remains, less than one whole frame (stride 3) -- genuinely no more complete
            // frames, not a straddle.
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

        // ── The degrade-path fix: a transient NEIGHBOUR-fill failure must not expose an
        // ── unconfirmed straddling frame, and must not be confused with true EOF ─────────────
        //
        // These use `set_fail_at_byte_offset` (not the single global `set_force_read_failure`,
        // which fails EVERY cluster including the one under test) to fail ONLY the neighbour
        // cluster's read, leaving the reader's own self-pinned cluster to fill normally --
        // isolating exactly the scenario the degrade path exists for.

        #[test]
        fn window_backward_degrade_retreats_the_pointer_past_the_unconfirmed_straddling_frame() {
            let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
            set_force_read_failure(false);
            set_fail_at_byte_offset(None);
            let (_handle, asset) = harness(geo24(3 * CLUSTER_SIZE as u64));

            // Cluster 1's read (byte_offset == 1 << CLUSTER_MAGNITUDE == 512) fails; cluster 0's
            // own read (byte_offset == 0) is untouched and succeeds normally.
            set_fail_at_byte_offset(Some(CLUSTER_SIZE));

            // Same straddling frame as the other backward straddle test (170 -> cluster 0, offset
            // 510, pre-degrade frame_count == 171 > 1).
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

            // THE bug this test catches: `ptr` must have retreated past the straddling frame
            // (byte_offset 510..513, which needs cluster 1's stitched slack) to the next-safest
            // one (byte_offset 507..510, entirely inside cluster 0's own plain payload) -- NOT
            // still point at byte_offset 510, which would hand the caller unconfirmed bytes.
            // SAFETY: `ptr` is cluster 0's pinned payload; 507..510 is entirely within its plain
            // (non-slack) `[0, 512)` payload span, always safe to read regardless of stitching.
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
            // A tiny (4-byte) cluster so a single straddling frame can be the ONLY servable frame
            // (see this module's own derivation: for CLUSTER_SIZE == 512 a straddle only ever
            // happens near the cluster's OWN tail, which can never coincide with "the only whole
            // frame from byte_offset" for any realistic byte_depth/num_channels -- a small
            // cluster is needed to make both true at once). `fill_logic::begin`'s sector math
            // (`cluster_size >> 9`) degenerates to 0 sectors here -- harmless: this test never
            // inspects byte content, only `frame_count`/`reader_ok()`.
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

            // Cluster 1's read (byte_offset == 1 << 2 == 4) fails; cluster 0's own read
            // (byte_offset == 0) succeeds.
            set_fail_at_byte_offset(Some(TINY_CLUSTER));

            // Frame 1 -> abs byte pos 3 -> cluster 0, offset 3: bytes_available = 4-3 = 1 <
            // stride(3) -- a straddle, AND (whole == 0) the ONLY frame this position could serve.
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
            // Same tiny-cluster shape as the forward version above, for the same reason.
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

            // Cluster 2's read (byte_offset == 2 << 2 == 8) fails; cluster 1's own read
            // (byte_offset == 1 << 2 == 4) succeeds.
            set_fail_at_byte_offset(Some(2 * TINY_CLUSTER));

            // Frame 2 -> abs byte pos 6 -> cluster 1, offset 2: byte_offset(2) + stride(3) = 5 >
            // resident(4) -- a straddle, AND (byte_offset/stride + 1 == 1) the ONLY frame
            // backward from here.
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

        // ── `Reader::read`, the stateless copy-out over the SAME handle ─────────────────
        //
        // Nested inside `window_tests` (rather than a sibling module) purely to reuse its harness
        // (`harness`/`geo`/`CLUSTER_SIZE`/`expected_cluster_bytes`) directly via `use super::*` --
        // a child module can see its parent's private items, so no re-derivation/duplication is
        // needed here, unlike `abi.rs`'s tests (a different FILE, which keeps its own small copy
        // per this crate's established convention).
        mod read_tests {
            use super::*;

            const STRIDE: usize = 2; // byte_depth 2 * num_channels 1, this module's `geo()` shape

            #[test]
            fn read_matches_a_manual_open_window_advance_loop_across_a_cluster_boundary() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                // 300 frames: all of cluster 0 (256 frames) plus 44 frames into cluster 1 --
                // spans the self-pin release/re-acquire `advance` performs across the boundary.
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

                // The SAME range, driven by hand over the same handle primitives `read` composes
                // -- proves `read` is not a second implementation of the frame-mapping/fill logic.
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
                        // SAFETY: `ptr` is `window`'s own pinned, resident chunk, valid for at
                        // least `frame_count * STRIDE` bytes, and `i < n * STRIDE <= frame_count *
                        // STRIDE`.
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
                // 10 bytes of real audio -> exactly 5 frames of stride 2; cluster 0 is short.
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
                // 3 full clusters -- plenty of real audio (768 frames); `dest_bytes` alone must
                // be the bounding factor here, not end-of-audio.
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                let num_frames: u32 = 300;
                // Deliberately not an even multiple of STRIDE, and far smaller than
                // `num_frames * STRIDE` (600): exercises both the frame-count bound AND the
                // "leftover partial-frame byte" truncation in the same call. `dest`'s backing
                // `Vec` is allocated at EXACTLY this length, so an out-of-bounds write would
                // corrupt the allocation, not just an assertion.
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

        // ── The passive peek: no lease, no fill, within-cluster only ────────────────────────────
        //
        // Reuses `harness`/`geo`/`CLUSTER_SIZE`/`expected_cluster_bytes` (`use super::*`), the
        // SAME synthetic ramp fixture `window_tests`'s own cases assert against.
        mod peek_tests {
            use super::*;

            const STRIDE: u32 = 2; // byte_depth 2 * num_channels 1, this module's `geo()` shape

            #[test]
            fn peek_forward_from_a_clusters_first_frame_runs_to_its_last_frame_matching_the_ramp() {
                let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
                set_force_read_failure(false);
                set_fail_at_byte_offset(None);
                let (_handle, asset) = harness(geo(3 * CLUSTER_SIZE as u64));

                // Make cluster 0 resident+ready via the normal fill path (`window()`, already
                // proven byte-exact against the ramp elsewhere in this module) -- and keep
                // `reader` alive so its self-pinned lease holds the chunk resident for `peek` to
                // observe; `peek` itself must take no pin of its own.
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
                // SAFETY: `reader`'s own held lease (still alive, `reader` not yet dropped) pins
                // this same chunk resident for the duration of this read-back.
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

                // Frame 100 -> byte_offset 200 (stride 2), well inside cluster 0 (resident 512).
                let start_frame = 100u64;
                let byte_offset = 200u32;
                let (frames, frame_count) = peek(asset, start_frame, -1);
                assert!(!frames.is_null());
                assert_eq!(
                    frame_count,
                    byte_offset / STRIDE + 1,
                    "a -1 peek must run back to (and include) the cluster's own first frame"
                );

                // `frames` points AT start_frame's own byte_offset (checkpoint-review I2) -- NOT
                // at the run's start. Walking backward `k` frames (`STRIDE` bytes each) must
                // land on the synthetic ramp's byte at `byte_offset - k*STRIDE`.
                for k in 0..frame_count {
                    // SAFETY: `frames - k*STRIDE` stays within cluster 0's plain payload
                    // (`byte_offset - k*STRIDE >= 0` for every `k < frame_count`, by construction
                    // of `frame_count` above), which `reader`'s still-held lease keeps resident.
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
                // 511 bytes, 24-bit mono (stride 3): cluster 0 is genuinely short (resident 511),
                // and 511 is NOT a whole multiple of stride 3 -- its last frame is partial, exactly
                // the boundary-straddling shape 24-bit samples hit constantly (and 16-bit never do).
                let (_handle, asset) = harness(geo24(511));

                // Fill+ready cluster 0 via the normal path (open at frame 0, not the partial tail),
                // keeping `reader` alive so its self-pin holds the chunk resident for the peek.
                let mut reader = Reader::open(asset, 0, 1, ReadHint::Cached);
                let (ptr, filled) = reader.window();
                assert!(!ptr.is_null(), "cluster 0 must fill from frame 0");
                assert_eq!(filled, 170, "511 bytes / stride 3 == 170 whole frames");

                // Frame 170 -> byte_offset 510, resident 511: only 1 byte remains forward, less
                // than one whole frame (stride 3). Residency is the NULL POINTER, not the count --
                // this is a valid resident position, so `frames` must be non-null with a 0 count.
                let (frames, frame_count) = peek(asset, 170, 1);
                assert!(
                    !frames.is_null(),
                    "the last partial frame is resident: residency is the pointer, not the count"
                );
                assert_eq!(
                    frame_count, 0,
                    "no WHOLE frame fits forward from the last partial frame -- 0, not EOF"
                );

                // `frames` points AT byte_offset 510 (the cursor's own frame); the consumer reads
                // the partial frame from here with its own byte-math. Byte 510 is cluster 0's own
                // ramp value (510 mod 256 == 254), byte 511 == 255 -- the same bytes `window()`'s
                // own 24-bit straddle test asserts at this position.
                // SAFETY: `reader`'s still-held lease pins cluster 0 resident; bytes 510/511 are in
                // its plain payload (resident 511), so both reads stay in bounds.
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

                // Geometry is registered, but nothing has ever requested/filled any cluster.
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

                // `request` reserves + constructs (no I/O) -- resident, but `ready` stays false
                // until a `mark_ready` this test deliberately never makes. Held for the test's
                // whole run (not dropped) so the chunk stays resident throughout.
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
