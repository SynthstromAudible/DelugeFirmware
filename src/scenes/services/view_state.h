#pragma once

/// @file
/// @brief What the scenes remember of how they were last looking at something: a waveform's scroll, zoom and loop
///        lock, the range last edited. View state, keyed by what it belongs to; never written to the song or a sound,
///        and not saved.

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

/// @brief Whose a remembered view is: a sound's source range, by the range's identity
///        (`bind::RuntimeFacts::range_identity`), or an audio clip, by its id. A range's view stays with the range
///        whatever file is loaded into it and wherever it moves among its source's ranges, as main keeps it on the
///        range's sample holder.
struct ViewKey {
	enum class Kind : uint8_t {
		RANGE,      ///< @ref id is the range's identity
		AUDIO_CLIP, ///< @ref id is the clip's id
	};
	Kind kind{Kind::RANGE};
	uint64_t id{0};
	bool operator==(const ViewKey&) const = default;
};

/// @brief How a waveform was last viewed.
struct SampleView {
	int64_t scroll{0};
	int64_t zoom{0};
	bool loop_locked{false};
};

/// @brief The scenes' view state for one song: a lookup for another song finds nothing.
class ViewStateStore {
public:
	/// @brief Empty the store unless it holds the view state of the song whose identity is @p song.
	void forget_unless(uint64_t song) {
		if (song != song_) {
			sample_views_.clear();
			song_ = song;
		}
	}
	/// @brief The view remembered for @p key, or nullopt.
	[[nodiscard]] std::optional<SampleView> sample_view(const ViewKey& key) const {
		for (const auto& [at, view] : sample_views_) {
			if (at == key) {
				return view;
			}
		}
		return std::nullopt;
	}
	void set_sample_view(const ViewKey& key, SampleView view) {
		for (auto& [at, held] : sample_views_) {
			if (at == key) {
				held = view;
				return;
			}
		}
		sample_views_.emplace_back(key, view);
	}
	/// @brief Give @p to the scroll and zoom remembered for @p from, as a cloned audio clip's sample holder takes the
	///        original's; nothing when none is remembered. The loop lock is not copied: main's clone copies the view
	///        alone.
	void copy_view(const ViewKey& from, const ViewKey& to) {
		if (const std::optional<SampleView> view = sample_view(from)) {
			set_sample_view(to, {.scroll = view->scroll, .zoom = view->zoom});
		}
	}
	/// @brief The clip the views have open, as a menu would open over it: what the global buttons and MIDI learn ask
	///        of "the current clip". The app tells the store; none until it does.
	[[nodiscard]] const MenuView& current_view() const { return current_view_; }
	void set_current_view(const MenuView& view) { current_view_ = view; }

private:
	uint64_t song_{0};
	std::vector<std::pair<ViewKey, SampleView>> sample_views_;
	MenuView current_view_{};
};

/// @brief The scenes' one store.
inline ViewStateStore& view_state() {
	static ViewStateStore store;
	return store;
}

} // namespace deluge::scenes::services
