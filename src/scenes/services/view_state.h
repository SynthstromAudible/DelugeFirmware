#pragma once

/// @file
/// @brief What the scenes remember of how they were last looking at something: a waveform's scroll, zoom and loop
///        lock, the range last edited. View state, keyed by address; never written to the song or a sound, and not
///        saved.

#include "deluge-project/instrument_slot.h"
#include "scenes/services/waveform_address.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace deluge::scenes::services {

/// @brief How a waveform was last viewed.
struct SampleView {
	int64_t scroll{0};
	int64_t zoom{0};
	bool loop_locked{false};
};

/// @brief The scenes' view state for one song: a lookup for another song finds nothing.
class ViewStateStore {
public:
	/// @brief Empty the store unless it holds @p song's view state.
	void forget_unless(const void* song) {
		if (song != song_) {
			sample_views_.clear();
			last_ranges_.clear();
			song_ = song;
		}
	}
	[[nodiscard]] std::optional<SampleView> sample_view(const WaveformAddress& address) const {
		for (const auto& [at, view] : sample_views_) {
			if (at == address) {
				return view;
			}
		}
		return std::nullopt;
	}
	void set_sample_view(const WaveformAddress& address, SampleView view) {
		for (auto& [at, held] : sample_views_) {
			if (at == address) {
				held = view;
				return;
			}
		}
		sample_views_.emplace_back(address, view);
	}
	[[nodiscard]] std::optional<uint16_t> last_range(project::SlotAddress slot, uint8_t source) const {
		for (const auto& [at, range] : last_ranges_) {
			if (at.first == slot && at.second == source) {
				return range;
			}
		}
		return std::nullopt;
	}
	void set_last_range(project::SlotAddress slot, uint8_t source, uint16_t range) {
		for (auto& [at, held] : last_ranges_) {
			if (at.first == slot && at.second == source) {
				held = range;
				return;
			}
		}
		last_ranges_.emplace_back(std::pair{slot, source}, range);
	}

private:
	const void* song_{nullptr};
	std::vector<std::pair<WaveformAddress, SampleView>> sample_views_;
	std::vector<std::pair<std::pair<project::SlotAddress, uint8_t>, uint16_t>> last_ranges_;
};

/// @brief The scenes' one store.
inline ViewStateStore& view_state() {
	static ViewStateStore store;
	return store;
}

} // namespace deluge::scenes::services
