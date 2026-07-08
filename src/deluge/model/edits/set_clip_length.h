#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class Clip;
class Action;

namespace deluge::edits {

/// Set a clip's loop length (the reification of the Song::setClipLength mutation behind the CLIP_LENGTH_INCREASE /
/// _DECREASE gestures). apply() routes the mutation through the command boundary; it self-records the undo into the
/// supplied Action (a Consequence keyed by the clip). The caller keeps the Action management — undo-a-shorten,
/// PATTERN_PASTE coalescing, resync — which is playback-coupled and shed in M2h/M3. mayReSyncClip mirrors the engine
/// arg (false for the linear-recording path, but that recorded-input site stays out of the funnel).
class SetClipLength final : public Edit {
public:
	SetClipLength(std::optional<project::ClipAddress> address, uint32_t newLength, Action* action, Clip* clip,
	              bool mayReSyncClip = true)
	    : address_(address), newLength_(newLength), action_(action), clip_(clip), mayReSyncClip_(mayReSyncClip) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::ClipAddress> address_;
	uint32_t newLength_;
	Action* action_;
	Clip* clip_; ///< Funnel-phase borrow (resolved from address_ at M3).
	bool mayReSyncClip_;
};

} // namespace deluge::edits
