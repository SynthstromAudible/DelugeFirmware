/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef LIBDELUGE_SAMPLE_SOURCE_H
#define LIBDELUGE_SAMPLE_SOURCE_H
#include "abi.h"
#include "types.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/// @brief Per-reader streaming cursor over one sample's residency.
///
/// Opaque; owned by the backend.
typedef struct DelugeSampleSource DelugeSampleSource;

/// @brief Immutable per-sample audio geometry, parsed once at load (above the port).
typedef struct DelugeSampleGeometry {
	uint32_t audio_data_start_bytes;  ///< byte offset of audio data within the file
	uint64_t audio_data_length_bytes; ///< length of the audio data, bytes
	uint32_t cluster_size_bytes;      ///< Cluster::size
	uint8_t byte_depth;               ///< bytes per channel-sample
	uint8_t num_channels;
	uint8_t raw_data_format; ///< RawDataFormat, opaque to the port
} DelugeSampleGeometry;

/// @brief Residency outcome of a region query.
///
/// Distinguishes the two outcomes the plain boolean `deluge_sample_region_acquire` collapses into
/// `false`: "not loaded YET" (worth waiting for) and "could not be reserved at all" (nothing is
/// coming — give up).
///
/// Each state carries a LEASE POLICY, which is part of the contract, not an implementation detail.
/// Numbered from 1 (not 0) so `if (state)` cannot be misread as a boolean — always compare against
/// a named constant.
///
/// @note Fixed underlying type (`: uint8_t`, C23 + C++11): this enum crosses the FFI BY VALUE
///       (`deluge_sample_region_acquire_ex`'s return), so its width must be pinned explicitly
///       rather than left to "whatever the C++ build's default `int` enum happens to be" — the
///       Rust side (`DelugeRegionState` in `crates/deluge_sample_source/src/abi.rs`) is
///       `#[repr(u8)]` to match. Every OTHER libdeluge enum stays a plain (unfixed, `int`-sized) C
///       enum and relies on bindgen no longer passing `-fshort-enums` (see
///       `src/bsp/rust/build.rs`) to agree with the C++ side's default int width instead.
typedef enum DelugeRegionState : uint8_t {
	/// Resident AND loaded. `out` is filled and the source holds the pin backing it; the caller may
	/// read `payload_base` until it releases the lease, closes, or acquires a different region.
	DELUGE_REGION_READY = 1,
	/// Reserved, leased and scheduled, but the data has not landed yet. `out` is NOT filled.
	/// The source RETAINS the lease on the region across the call, so the background fill keeps
	/// progressing (and the region cannot be stolen) while the caller defers and retries. Retrying
	/// the same index is idempotent — it does not accumulate leases. The retained lease is dropped
	/// by the next acquire on this source or by `deluge_sample_source_close`.
	DELUGE_REGION_LOADING,
	/// The region could not be reserved or constructed at all (out of RAM / nothing stealable / out
	/// of range / null source). `out` is NOT filled and NOTHING is left leased for it — no fill is
	/// in flight, so retrying gains the caller nothing.
	DELUGE_REGION_UNAVAILABLE,
} DelugeRegionState;

/// @brief One acquired, pinned, borrowed region of resident sample data.
typedef struct DelugeSampleRegion {
	void* payload_base;      ///< StreamedChunk payload().data() for the resident cluster (pinned)
	uint32_t region_index;   ///< cluster index this region corresponds to
	uint32_t resident_bytes; ///< valid payload bytes in this region (Cluster::size, or short for the last)
	uint64_t lease;          ///< opaque pin token; retain/release the independent pin with it
} DelugeSampleRegion;

/// @brief Open a per-reader cursor over one sample's residency. [task] [audio]
///
/// @param stream_backing The sample's stream, which identifies its residency.
/// @param geometry        The sample's immutable audio geometry.
/// @return A new cursor, or `nullptr` on failure (e.g. a null @p stream_backing or no
///         resource manager yet).
DelugeSampleSource* deluge_sample_source_open(DelugeStreamBacking* stream_backing,
                                              DelugeSampleGeometry geometry) DELUGE_NOEXCEPT;

/// @brief Make the region containing cluster @p index resident-or-scheduled, pin it, and report
///        which. [task] [audio]
///
/// Non-blocking (CLUSTER_ENQUEUE semantics), allocation-free. On DELUGE_REGION_READY the next
/// cluster in @p direction is also prefetched.
///
/// @p out is filled only on DELUGE_REGION_READY, and only then does the source release the region
/// it previously handed out (so the caller need not release before re-acquiring). The other two
/// states leave the standing current region alone — a deferring caller keeps reading what it
/// already has. See DelugeRegionState for each state's lease policy; in particular
/// DELUGE_REGION_LOADING keeps the region leased so the fill continues across the caller's
/// defer/retry cycle.
///
/// If @p index is exactly the standing prefetched neighbour, that lease is PROMOTED into this
/// call's result rather than re-fetched — which, on a LOADING outcome, empties the prefetch slot
/// and moves the reservation to `pending` (see deluge_sample_region_state: querying @p index
/// afterwards still finds it, now via `pending` instead of `prefetch`, so the query stays
/// truthful).
/// @param src       The cursor to acquire on.
/// @param index     Cluster index to make resident.
/// @param direction Prefetch direction on a READY outcome: +1 (forward) or -1 (reverse).
/// @param priority  Loader priority; used only when the fill is enqueued.
/// @param out       Filled with the resident region only on DELUGE_REGION_READY.
/// @return The residency outcome; see DelugeRegionState.
DelugeRegionState deluge_sample_region_acquire_ex(DelugeSampleSource* src, uint32_t index, int8_t direction,
                                                  uint32_t priority, DelugeSampleRegion* out) DELUGE_NOEXCEPT;

/// @brief Boolean form of deluge_sample_region_acquire_ex: true == DELUGE_REGION_READY. [task] [audio]
///
/// Callers that cannot act on the LOADING/UNAVAILABLE distinction (both are simply "NotReady")
/// use this.
/// @param src       The cursor to acquire on.
/// @param index     Cluster index to make resident.
/// @param direction Prefetch direction on a READY outcome: +1 (forward) or -1 (reverse).
/// @param priority  Loader priority; used only when the fill is enqueued.
/// @param out       Filled with the resident region only when the region is ready.
/// @return true if @p out was filled (DELUGE_REGION_READY); false otherwise.
bool deluge_sample_region_acquire(DelugeSampleSource* src, uint32_t index, int8_t direction, uint32_t priority,
                                  DelugeSampleRegion* out) DELUGE_NOEXCEPT;

/// @brief Residency of @p index as tracked by this cursor RIGHT NOW, WITHOUT acquiring anything. [audio]
///
/// No lease taken, no fetch scheduled, no state changed. This consults every reservation the
/// cursor is currently holding -- `current`, the standing `prefetch`, and any retained `pending`
/// LOADING reservation from a previous acquire_ex call -- and reports on whichever of them is
/// tracking @p index, by matching that reservation's OWN cluster index (never by assuming
/// @p index is "the neighbour" or "the last call's subject").
///
/// Because the match is by index, this cannot conflate two different clusters the way an unindexed
/// query would: after an acquire_ex(0) READY followed by an acquire_ex(5) LOADING (index 5 jumped
/// ahead of the standing prefetch), deluge_sample_region_state(src, 6) still reports on the TRUE
/// neighbour (index 6, tracked by `prefetch` if in range, else UNAVAILABLE) — it does not fall back
/// to reporting index 5's LOADING state just because index 5 is what the cursor most recently
/// resolved.
/// @param src   The cursor to query; `nullptr` reports DELUGE_REGION_UNAVAILABLE.
/// @param index Cluster index to query.
/// @return DELUGE_REGION_READY if @p index is tracked by this cursor and its data has landed;
///         DELUGE_REGION_LOADING if @p index is tracked (as `pending` or `prefetch`) but has not
///         landed yet, with a fill in flight; DELUGE_REGION_UNAVAILABLE if this cursor has
///         nothing in flight or resident for @p index right now (it isn't `current`, `pending`,
///         or `prefetch`). UNAVAILABLE means only "ask me again after a different acquire_ex
///         call" — it is not a claim that @p index can never load; a fresh acquire_ex(index, ...)
///         may still succeed.
DelugeRegionState deluge_sample_region_state(const DelugeSampleSource* src, uint32_t index) DELUGE_NOEXCEPT;

/// @brief Take an INDEPENDENT pin on the region's chunk, keyed on the opaque @p lease token alone
///        (no cursor involved). [task] [audio]
///
/// Separate from the cursor's current/prefetch/pending pins (those are managed by acquire/close).
/// @p lease == 0 is a no-op. The token comes from a READY `DelugeSampleRegion::lease`.
/// @param lease Pin token from a READY `DelugeSampleRegion::lease`, or 0 for a no-op.
void deluge_sample_region_retain(uint64_t lease) DELUGE_NOEXCEPT;

/// @brief Drop an INDEPENDENT pin previously taken with deluge_sample_region_retain, keyed on the
///        opaque @p lease token alone (no cursor involved). [task] [audio]
///
/// @p lease == 0 is a no-op.
/// @warning PAIRING: release only a token you personally retained. Bare-releasing a token the
///          cursor still holds as current/prefetch/pending would drop the cursor's own pin early,
///          exposing that chunk to eviction while the cursor still believes it is pinned.
/// @param lease Pin token previously passed to deluge_sample_region_retain, or 0 for a no-op.
void deluge_sample_region_release(uint64_t lease) DELUGE_NOEXCEPT;

/// @brief Close the cursor, releasing any leases it still holds (current + prefetch). [task] [audio]
///
/// @param src The cursor to close.
void deluge_sample_source_close(DelugeSampleSource* src) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#endif // LIBDELUGE_SAMPLE_SOURCE_H
