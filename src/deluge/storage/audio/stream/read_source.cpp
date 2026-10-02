#include "storage/audio/stream/read_source.h"

namespace deluge::audio::stream {

std::expected<uint32_t, DelugeStatus> EfatfsReadSource::read(uint32_t cluster_index, std::span<std::byte> dst) {
	uint32_t out_read = 0;
	uint32_t byte_offset = cluster_index << cluster_size_magnitude_;
	const DelugeStatus status =
	    deluge_efatfs_read_at(handle_, byte_offset, dst.data(), static_cast<uint32_t>(dst.size()), &out_read);
	if (status != DELUGE_OK) {
		return std::unexpected(status);
	}
	return out_read;
}

std::expected<uint32_t, DelugeStatus> SampleStreamReadSource::read(uint32_t cluster_index, std::span<std::byte> dst) {
	uint32_t out_read = 0;
	uint32_t byte_offset = cluster_index << cluster_size_magnitude_;
	const DelugeStatus status =
	    deluge_sample_stream_read_at(stream_handle_, byte_offset, reinterpret_cast<uint8_t*>(dst.data()),
	                                 static_cast<uint32_t>(dst.size()), &out_read);
	if (status != DELUGE_OK) {
		return std::unexpected(status);
	}
	return out_read;
}

} // namespace deluge::audio::stream
