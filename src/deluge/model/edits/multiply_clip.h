#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"
#include <optional>

class ModelStackWithTimelineCounter;
class Action;

namespace deluge::edits {

/// @brief Double an instrument clip's length, duplicating its content (CLIP_MULTIPLY).
///
/// apply() opens the Action, records the ConsequenceInstrumentClipMultiply BEFORE the mutation (redo ordering),
/// then doubles the clip. The caller owns the max-length guard, the zoom, and the popup; action() exposes the
/// created Action so the view can stamp the Action's AFTER zoom/scroll view-state.
class MultiplyClip final : public Edit {
public:
	/// @param address     Durable clip address (nullopt if unaddressable).
	/// @param engineStack Borrowed stack naming the clip to double; must outlive apply().
	MultiplyClip(std::optional<project::ClipAddress> address, ModelStackWithTimelineCounter* engineStack)
	    : address_(address), engineStack_(engineStack) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

	/// @brief The Action this Edit opened, so the caller can stamp its AFTER view-state.
	/// @return The Action, or nullptr if none was opened (before apply(), or on failure to open).
	[[nodiscard]] Action* action() const { return action_; }

private:
	std::optional<project::ClipAddress> address_;
	ModelStackWithTimelineCounter* engineStack_; ///< Engine borrow.
	Action* action_ = nullptr;
};

} // namespace deluge::edits
