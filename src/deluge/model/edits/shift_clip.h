#pragma once

#include "deluge-project/clip_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithTimelineCounter;

namespace deluge::edits {

/// @brief Shift a whole clip horizontally (CLIP_HORIZONTAL_SHIFT).
///
/// apply() does the engine shift, then records/coalesces the undo: it extends an open same-clip shift Action's
/// ConsequenceClipHorizontalShift amount, reuses a matching consequence-less Action, or opens a new Action +
/// Consequence. shifted() exposes shiftHorizontally's result so the caller can bail when nothing moved
/// (cf. AddNote::added()). The clip-tier twin of ShiftNoteRow.
class ShiftClip final : public Edit {
public:
	ShiftClip(std::optional<project::ClipAddress> address, int32_t shiftAmount, bool shiftAutomation,
	          bool shiftSequenceAndMPE, ModelStackWithTimelineCounter* engineStack)
	    : address_(address), shiftAmount_(shiftAmount), shiftAutomation_(shiftAutomation),
	      shiftSequenceAndMPE_(shiftSequenceAndMPE), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

	/// @brief Whether the engine actually shifted (false = the caller should bail without rendering/recording).
	/// @return True if shiftHorizontally moved the clip.
	[[nodiscard]] bool shifted() const { return shifted_; }

private:
	std::optional<project::ClipAddress> address_;
	int32_t shiftAmount_;
	bool shiftAutomation_;
	bool shiftSequenceAndMPE_;
	ModelStackWithTimelineCounter* engineStack_; ///< Borrowed for the duration of apply().
	bool shifted_ = false;
};

} // namespace deluge::edits
