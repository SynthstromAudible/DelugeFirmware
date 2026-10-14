#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class Clip;
class Action;

namespace deluge::edits {

/// @brief Set a clip's loop length (the Song::setClipLength mutation behind the CLIP_LENGTH_INCREASE / _DECREASE
/// gestures).
///
/// apply() self-records the undo into the supplied Action (a Consequence keyed by the clip). The caller owns the
/// Action management (undo-a-shorten, PATTERN_PASTE coalescing, resync), which is playback-coupled.
class SetClipLength final : public Edit {
public:
	/// @param address       Model address of the clip, if it has one.
	/// @param newLength     New loop length in ticks.
	/// @param action        Action that receives the undo Consequence.
	/// @param clip          The clip to resize (borrowed for the duration of apply()).
	/// @param mayReSyncClip Passed through to Song::setClipLength; false on the linear-recording path.
	SetClipLength(std::optional<project::ClipAddress> address, uint32_t newLength, Action* action, Clip* clip,
	              bool mayReSyncClip = true)
	    : address_(address), newLength_(newLength), action_(action), clip_(clip), mayReSyncClip_(mayReSyncClip) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::ClipAddress> address_;
	uint32_t newLength_;
	Action* action_;
	Clip* clip_; ///< Borrowed for the duration of apply().
	bool mayReSyncClip_;
};

} // namespace deluge::edits
