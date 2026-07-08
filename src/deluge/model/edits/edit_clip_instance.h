#pragma once

#include "model/edits/edit.h"
#include <cstdint>

class Output;
class ClipInstance;
class Clip;
class Action;

namespace deluge::edits {

/// Change an arrangement clip-instance's pos/length/clip (the reification of the arranger's resize / swap /
/// drag-within-row gestures, which all call ClipInstance::change). apply() routes the self-recording change() through
/// the command boundary; it records a ConsequenceClipInstanceChange into the supplied Action (reused — no new
/// Consequence). Mutation-only, like SetClipLength: the site keeps its getNewAction, arrangement.rowEdited
/// notifications, and rendering.
///
/// Funnel-phase engine borrow only — ClipInstance has no durable id (it is keyed by mutable pos), so there is no
/// Address here. A stable clip-instance id is an M3 prerequisite (resolve-at-use), to be designed then.
class EditClipInstance final : public Edit {
public:
	EditClipInstance(Output* output, ClipInstance* clipInstance, int32_t newPos, int32_t newLength, Clip* newClip,
	                 Action* action)
	    : output_(output), clipInstance_(clipInstance), newPos_(newPos), newLength_(newLength), newClip_(newClip),
	      action_(action) {}

	Error apply(Context& ctx) override;

private:
	Output* output_;
	ClipInstance* clipInstance_; ///< Funnel-phase engine borrow (no durable address yet — M3 prereq).
	int32_t newPos_;
	int32_t newLength_;
	Clip* newClip_;
	Action* action_;
};

} // namespace deluge::edits
