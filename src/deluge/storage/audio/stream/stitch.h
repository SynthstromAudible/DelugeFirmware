#pragma once
#include "storage/audio/audio_file_format.h" // RawDataFormat
#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::audio::stream {

/// @brief Mutable view of the boundary bytes at the tail of the cluster immediately before the one
///        being stitched.
///
/// `tail` is a view of `prevCluster->payload()` over `[cluster_size-4, cluster_size+7)` (11 bytes), so
/// `tail[4+k] == prevCluster->payload()[cluster_size+k]` and `tail[0..4) == prevCluster->payload()[size-4..size)`.
struct StitchPrevEdge {
	std::span<std::byte> tail;    ///< 11 bytes: prevCluster->payload()[cluster_size-4 .. cluster_size+7)
	bool* end_boundary_converted; ///< Points at prevCluster->extra_bytes_at_end_converted.
};

/// @brief View of the boundary bytes at the head of the cluster immediately after the one being
///        stitched: a mutable span plus the read-only pre-conversion bytes.
struct StitchNextEdge {
	std::span<std::byte> head;                      ///< 7 bytes: nextCluster->payload()[0..7)
	std::span<const std::byte, 3> unconverted_head; ///< nextCluster->first_three_bytes_pre_data_conversion
	bool* start_boundary_converted;                 ///< Points at nextCluster->extra_bytes_at_start_converted.
};

/// @brief Stitch the boundary bytes of a single cluster against its already-loaded neighbors, in place.
///
/// Pure function with no Cluster/Sample/AudioEngine reach. Mutates `self_data` (this cluster's data plus
/// overhang; must have `cluster_size+7` bytes usable), the neighbor edge spans, and the four boundary
/// flags. `prev`/`next` are nullptr when that neighbor is absent or not yet loaded -- the caller only
/// supplies a loaded neighbor.
///
/// @param self_data                     This cluster's raw data, `cluster_size+7` bytes usable.
/// @param cluster_index                 Index of this cluster within the sample.
/// @param format                        Raw sample data format.
/// @param audio_data_start_pos_bytes    Byte offset of the sample's audio data within the file.
/// @param cluster_size                  Cluster size in bytes.
/// @param self_start_boundary_converted In/out: whether this cluster's start boundary has been converted.
/// @param self_end_boundary_converted   In/out: whether this cluster's end boundary has been converted.
/// @param prev                          Previous cluster's boundary edge, or nullptr if unavailable.
/// @param next                          Next cluster's boundary edge, or nullptr if unavailable.
void stitch_boundaries(std::span<std::byte> self_data, int32_t cluster_index, RawDataFormat format,
                       uint32_t audio_data_start_pos_bytes, size_t cluster_size, bool& self_start_boundary_converted,
                       bool& self_end_boundary_converted, StitchPrevEdge* prev, StitchNextEdge* next);

} // namespace deluge::audio::stream
