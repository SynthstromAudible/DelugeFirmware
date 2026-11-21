#pragma once

/// @file
/// @brief What a scene reads of a clip that its record does not hold: its engine's play state and position, and the
///        view state the clip keeps.

#include "deluge-project/clip_address.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace deluge::scenes::bind {

/// @brief What an audio clip's view needs of the clip that no record holds.
struct AudioClipFact {
	uint32_t track_id{0}; ///< the audio track it plays on: the record holds no clip-to-track index
	int32_t max_zoom{0};  ///< the zoom that fits the clip's whole length, which a take being recorded sets
	bool unarmed{true};   ///< no launch or stop is armed on it
	bool reversed{false}; ///< it plays its sample reversed now: its record's reversed, inverted while it runs backwards
	int32_t session_index{-1}; ///< its index among the session's clips, by which a dirty set names clips
	bool operator==(const AudioClipFact&) const = default;
};

/// @brief The isomorphic keyboard's layout state, as the current clip keeps it (`KeyboardState::isomorphic`).
struct IsomorphicKeyboard {
	int32_t scroll_offset = 0; ///< the note on the bottom-left pad
	int32_t row_interval = 5;  ///< the semitones from one row to the next
	bool operator==(const IsomorphicKeyboard&) const = default;
};

/// @brief Where the clip view's timeline was left: the song's clip navigation, which every clip view shares.
struct ClipNavigation {
	int32_t x_scroll{0};        ///< the first tick shown
	uint32_t x_zoom{0};         ///< ticks per square
	bool triplets_on{false};    ///< the triplets view is on
	uint32_t triplets_level{0}; ///< the triplets view's level, in ticks
	bool operator==(const ClipNavigation&) const = default;
};

/// @brief A clip's facts, answered by the host.
class ClipFacts {
public:
	virtual ~ClipFacts() = default;
	/// @brief What the audio clip @p clip's view needs of it, or nullopt where the song holds no such audio clip.
	[[nodiscard]] virtual std::optional<AudioClipFact> audio_clip(project::ClipAddress clip) const = 0;
	/// @brief Whether @p clip plays now: the transport runs, the clip is active, and no count-in is running.
	[[nodiscard]] virtual bool clip_playing(project::ClipAddress clip) const = 0;
	/// @brief @p clip's play position now, in ticks.
	[[nodiscard]] virtual int32_t clip_live_pos(project::ClipAddress clip) const = 0;
	/// @brief Whether @p clip plays only in the arrangement (a take or an arranger clip), not in the session.
	[[nodiscard]] virtual bool clip_arrangement_only(project::ClipAddress clip) const = 0;
	/// @brief Whether @p clip records a take linearly now.
	[[nodiscard]] virtual bool clip_recording_linearly(project::ClipAddress clip) const = 0;
	/// @brief The view of the audio clip @p clip at @p scroll and @p zoom ticks, in samples: what the waveform scan
	/// takes. A take recorded without the clock measures from what it has captured.
	/// @return The scroll and the zoom (samples per column); {0, 0} where there is no such audio clip.
	[[nodiscard]] virtual std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress clip, int32_t scroll,
	                                                                     int32_t zoom) const = 0;
	/// @brief The keyboard screen's isomorphic layout under the menu: its state when the root UI is the keyboard on
	/// that layout, else none.
	[[nodiscard]] virtual std::optional<IsomorphicKeyboard> isomorphic_keyboard() const = 0;
	/// @brief The song's clip navigation: the scroll, the zoom and the triplets view every clip view starts from.
	/// @return The navigation; none with no song.
	[[nodiscard]] virtual std::optional<ClipNavigation> clip_navigation() const = 0;
};

} // namespace deluge::scenes::bind
