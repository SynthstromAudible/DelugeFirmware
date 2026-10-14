#pragma once

#include "model/edits/edit.h"
#include <cstdint>

class Output;
class ClipInstance;
class Clip;
class Action;

namespace deluge::edits {

/// @brief Change an arrangement clip-instance's pos/length/clip.
///
/// Covers the arranger's resize / swap / drag-within-row gestures, which all call ClipInstance::change(). apply()
/// routes the self-recording change() through the command boundary; it records a ConsequenceClipInstanceChange into
/// the supplied Action (no new Consequence). Mutation-only, like SetClipLength: the caller owns getNewAction,
/// arrangement.rowEdited notifications, and rendering.
///
/// Engine borrow only: ClipInstance has no durable id (it is keyed by mutable pos), so there is no address.
class EditClipInstance final : public Edit {
public:
	/// @param output       The output whose arrangement row holds the instance.
	/// @param clipInstance The instance to change (engine borrow).
	/// @param newPos       New start position, in ticks.
	/// @param newLength    New length, in ticks.
	/// @param newClip      The clip the instance should refer to.
	/// @param action       The Action that receives the ConsequenceClipInstanceChange.
	EditClipInstance(Output* output, ClipInstance* clipInstance, int32_t newPos, int32_t newLength, Clip* newClip,
	                 Action* action)
	    : output_(output), clipInstance_(clipInstance), newPos_(newPos), newLength_(newLength), newClip_(newClip),
	      action_(action) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	Output* output_;
	ClipInstance* clipInstance_; ///< Engine borrow (no durable address).
	int32_t newPos_;
	int32_t newLength_;
	Clip* newClip_;
	Action* action_;
};

} // namespace deluge::edits
