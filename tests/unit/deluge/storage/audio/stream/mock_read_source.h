#pragma once

#include "storage/audio/stream/read_source.h"
#include <algorithm>
#include <vector>

namespace deluge::audio::stream {

// In-memory ReadSource for specs: one byte vector per cluster index. No card, no FatFS.
class MockReadSource final : public ReadSource {
public:
	explicit MockReadSource(std::vector<std::vector<std::byte>> clusters) : clusters_{std::move(clusters)} {}

	std::expected<uint32_t, DelugeStatus> read(uint32_t cluster_index, std::span<std::byte> dst) override {
		if (cluster_index >= clusters_.size()) {
			return std::unexpected(DELUGE_ERR_PARAM);
		}
		const auto& src = clusters_[cluster_index];
		uint32_t n = static_cast<uint32_t>(std::min(dst.size(), src.size()));
		std::copy_n(src.begin(), n, dst.begin());
		return n;
	}

private:
	std::vector<std::vector<std::byte>> clusters_;
};

} // namespace deluge::audio::stream
