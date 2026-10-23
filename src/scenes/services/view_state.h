#pragma once

/// @file
/// @brief What the scenes remember of how they were last looking at something: a waveform's scroll, zoom and loop
///        lock, the range last edited. View state, keyed by address; never written to the song or a sound, and not
///        saved.

#include "deluge-project/clip_address.h"
#include "deluge-project/drum_type.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/setting_group.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/services/waveform_address.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::scenes::services {

/// @brief What a menu opens over, as the view shows it: the clip and its track, what plays it, the kit row selected and
///        the clip's affect-entire. The view's facts, which the song's records do not hold; the app fills it from the
///        view it opens the menu from.
struct MenuView {
	std::optional<project::ClipAddress> clip;   ///< the clip; none for a song-level or settings menu
	std::optional<project::TrackAddress> track; ///< the clip's track
	bind::TrackKind kind{bind::TrackKind::SYNTH};
	bool audio_clip{false};               ///< the clip is an audio clip
	std::optional<uint32_t> selected_row; ///< a kit's selected row, by its drum id
	project::DrumType selected_row_type{project::DrumType::SOUND};
	bool row_in_clip{false};   ///< the selected row has its note row in the clip
	bool affect_entire{false}; ///< the kit clip's affect-entire
	bool operator==(const MenuView&) const = default;
};

/// @brief How a waveform was last viewed.
struct SampleView {
	int64_t scroll{0};
	int64_t zoom{0};
	bool loop_locked{false};
	std::string file; ///< the file it was viewing, which the view belongs to
};

/// @brief The scenes' view state for one song: a lookup for another song finds nothing.
class ViewStateStore {
public:
	/// @brief Empty the store unless it holds the view state of the song whose identity is @p song.
	void forget_unless(uint64_t song) {
		if (song != song_) {
			sample_views_.clear();
			last_ranges_.clear();
			song_ = song;
		}
	}
	/// @brief The view remembered at @p address while it showed @p file, or nullopt.
	[[nodiscard]] std::optional<SampleView> sample_view(const WaveformAddress& address, std::string_view file) const {
		const std::optional<SampleView> view = sample_view(address);
		return view && view->file == file ? view : std::nullopt;
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
	/// @brief The clip the views have open, as a menu would open over it: what the global buttons and MIDI learn ask
	///        of "the current clip". The app tells the store; none until it does.
	[[nodiscard]] const MenuView& current_view() const { return current_view_; }
	void set_current_view(const MenuView& view) { current_view_ = view; }

private:
	uint64_t song_{0};
	std::vector<std::pair<WaveformAddress, SampleView>> sample_views_;
	std::vector<std::pair<std::pair<project::SlotAddress, uint8_t>, uint16_t>> last_ranges_;
	MenuView current_view_{};
};

/// @brief The scenes' one store.
inline ViewStateStore& view_state() {
	static ViewStateStore store;
	return store;
}

} // namespace deluge::scenes::services
