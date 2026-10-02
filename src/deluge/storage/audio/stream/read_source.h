#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

extern "C" {
#include "libdeluge/file_io.h" // deluge_efatfs_read_at
#include "libdeluge/sample_stream.h"
#include "libdeluge/types.h" // DelugeHandle, DelugeStatus
}

namespace deluge::audio::stream {

/// @brief The audio-stream module's read seam (design spec §6/§7).
///
/// A ReadSource pulls one FAT-cluster-sized block of a sample's on-card bytes into a caller buffer.
/// SampleStreamReadSource (via the `deluge_sample_stream` registry handle) is what
/// SampleStream::make_read_source() actually returns. EfatfsReadSource (direct embedded-fatfs handle
/// reads) remains for callers that hold a raw efatfs handle outside the registry. The reconstruction
/// core reads through this and stays pure.
class ReadSource {
public:
	virtual ~ReadSource() = default;

	/// @brief Read exactly dst.size() bytes for the given cluster.
	///
	/// @param cluster_index Cluster index; the byte offset is cluster_index << magnitude.
	/// @param dst           Destination buffer; exactly dst.size() bytes are read on success.
	/// @return Bytes read on success, or a DelugeStatus error.
	virtual std::expected<uint32_t, DelugeStatus> read(uint32_t cluster_index, std::span<std::byte> dst) = 0;
};

/// @brief Reads via the embedded-fatfs handle at a cluster-aligned byte offset
///        (deluge_efatfs_read_at). Selected when the sample has an open efatfs handle.
class EfatfsReadSource final : public ReadSource {
public:
	/// @param handle                 The open embedded-fatfs read handle.
	/// @param cluster_size_magnitude log2(cluster size) -- cluster size expressed as a shift amount.
	EfatfsReadSource(DelugeHandle handle, uint8_t cluster_size_magnitude)
	    : handle_{handle}, cluster_size_magnitude_{cluster_size_magnitude} {}

	/// @copydoc ReadSource::read
	std::expected<uint32_t, DelugeStatus> read(uint32_t cluster_index, std::span<std::byte> dst) override;

private:
	DelugeHandle handle_;
	uint8_t cluster_size_magnitude_;
};

/// @brief Reads via a `deluge_sample_stream` registry handle at a cluster-aligned byte offset
///        (deluge_sample_stream_read_at). Returned by SampleStream::make_read_source().
class SampleStreamReadSource final : public ReadSource {
public:
	/// @param stream_handle          The `deluge_sample_stream` registry handle.
	/// @param cluster_size_magnitude log2(cluster size) -- cluster size expressed as a shift amount.
	SampleStreamReadSource(DelugeHandle stream_handle, uint8_t cluster_size_magnitude)
	    : stream_handle_{stream_handle}, cluster_size_magnitude_{cluster_size_magnitude} {}

	/// @copydoc ReadSource::read
	std::expected<uint32_t, DelugeStatus> read(uint32_t cluster_index, std::span<std::byte> dst) override;

private:
	DelugeHandle stream_handle_;
	uint8_t cluster_size_magnitude_;
};

// The ReadSource is constructed by deluge::audio::stream::SampleStream
// (SampleStream::make_read_source(), storage/audio/stream/sample_stream.h) -- it owns the Sample's
// read-stream handle, so it's the only place that can build one without a caller branching on Sample
// internals.

} // namespace deluge::audio::stream
