#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"
#include <optional>

class ModelStackWithTimelineCounter;

namespace deluge::edits {

/// Clear a whole clip's contents (the reification of the CLIP_CLEAR gesture, shared by instrument and audio clips —
/// clear() is virtual). apply() opens the Action and calls clip->clear(), which self-records the undo. The view keeps
/// the clearAutomation/clearSequenceAndMPE decision, the popup, and the render.
class ClearClip final : public Edit {
public:
	ClearClip(std::optional<project::ClipAddress> address, bool clearAutomation, bool clearSequenceAndMPE,
	          ModelStackWithTimelineCounter* engineStack)
	    : address_(address), clearAutomation_(clearAutomation), clearSequenceAndMPE_(clearSequenceAndMPE),
	      engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::ClipAddress> address_;
	bool clearAutomation_;
	bool clearSequenceAndMPE_;
	ModelStackWithTimelineCounter* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
