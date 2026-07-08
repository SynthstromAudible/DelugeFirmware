#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"
#include <optional>

class ModelStackWithTimelineCounter;
class Action;

namespace deluge::edits {

/// Double an instrument clip's length, duplicating its content (the reification of CLIP_MULTIPLY /
/// doubleClipLengthAction). apply() opens the Action, records the ConsequenceInstrumentClipMultiply BEFORE the
/// mutation (redo ordering), then doubles the clip. The view keeps the max-length guard, the zoom, and the popup;
/// action() exposes the created Action so the view can stamp its AFTER zoom/scroll (funnel-phase; Action view-state
/// is shed in M2h).
class MultiplyClip final : public Edit {
public:
	MultiplyClip(std::optional<project::ClipAddress> address, ModelStackWithTimelineCounter* engineStack)
	    : address_(address), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

	/// The Action this Edit opened (or nullptr), so the caller can stamp its AFTER view-state.
	[[nodiscard]] Action* action() const { return action_; }

private:
	std::optional<project::ClipAddress> address_;
	ModelStackWithTimelineCounter* engineStack_; ///< Funnel-phase borrow.
	Action* action_ = nullptr;
};

} // namespace deluge::edits
