#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"

#include <cstdint>

class Action;

namespace deluge::edits {

/// @brief Resize an audio clip by one of its length gestures, as one undo step.
///
/// apply() owns the gesture's undo step: the Action (a lengthen right after a shorten of the same clip reverts the
/// shorten and, with the clock running, re-syncs the clip; successive shortens coalesce; a paste being built absorbs
/// either), the sample marker's move through the clip's AUDIO_CLIP row, the length (SetClipLength), and the length
/// consequence's marker, so an undo restores both.
///
/// @note The coalescing compares each undo step's clip with this one, and a step takes its clip from the song's current
///       clip: resize the clip the user is looking at, as its view does.
class ResizeAudioClip final : public Edit {
public:
	/// @brief What the gesture moves.
	enum class Kind : uint8_t {
		END,              ///< the sample's end (its start, reversed) dragged to a new clip length
		START,            ///< the sample's start (its end, reversed) dragged to a new start tick
		STRETCH,          ///< the clip lengthened or shortened, the sample timestretched to fill it
		NO_STRETCH,       ///< STRETCH, then the sample's end moved with it so it keeps its speed
		TO_SAMPLE_LENGTH, ///< the clip's length set to its sample's
	};

	/// @param clip       The audio clip.
	/// @param kind       What the gesture moves.
	/// @param new_length The new length in ticks; START's new start tick; not read for TO_SAMPLE_LENGTH.
	/// @param old_length The length the gesture started from, which says whether it lengthens or shortens.
	ResizeAudioClip(project::ClipAddress clip, Kind kind, int32_t new_length, int32_t old_length)
	    : clip_(clip), kind_(kind), new_length_(new_length), old_length_(old_length) {}

	/// @copydoc Edit::apply
	/// @return Error::UNSPECIFIED where the clip is not an audio clip of the song, START or TO_SAMPLE_LENGTH has no
	/// sample to measure, or NO_STRETCH asks for no length or more than kMaxSequenceLength.
	Error apply(Context& ctx) override;

	/// @brief The undo step the clip's length change was logged in (not a marker drag's), or null: what a redo of it
	/// scrolls the view back to, which the view learns only after it scrolls.
	[[nodiscard]] ::Action* lengthAction() const { return length_action_; }

private:
	project::ClipAddress clip_;
	Kind kind_;
	int32_t new_length_;
	int32_t old_length_;
	::Action* length_action_{nullptr};
};

} // namespace deluge::edits
