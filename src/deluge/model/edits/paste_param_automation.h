#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include "model/edits/set_param_value.h"
#include <cstdint>

class CopiedParamAutomation;

namespace deluge::edits {

/// @brief Paste clipboard automation into a param region.
///
/// apply() opens the AUTOMATION_PASTE Action, pre-snapshots the before-state, then delegates to AutoParam::paste.
class PasteParamAutomation final : public Edit {
public:
	/// @brief The param at @p address, which apply() finds in the song itself as @p resolve says; a knob's paste
	///        resolves as a write: what it pastes into is made (a cable, a lane), the take while the arrangement
	///        records.
	/// @param address     The param's address.
	/// @param startPos    Start of the destination region, in ticks.
	/// @param endPos      End of the destination region, in ticks.
	/// @param scaleFactor Horizontal scale applied to the pasted automation.
	/// @param copied      The clipboard automation (borrowed).
	/// @param resolve     How apply() finds the param.
	PasteParamAutomation(project::ParamAddress address, int32_t startPos, int32_t endPos, float scaleFactor,
	                     CopiedParamAutomation* copied, SetParamValue::Resolve resolve = SetParamValue::Resolve::WRITE)
	    : address_(address), startPos_(startPos), endPos_(endPos), scaleFactor_(scaleFactor), copied_(copied),
	      resolve_(resolve) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	project::ParamAddress address_;
	int32_t startPos_;
	int32_t endPos_;
	float scaleFactor_;
	CopiedParamAutomation* copied_; ///< Clipboard borrow.
	SetParamValue::Resolve resolve_;
};

} // namespace deluge::edits
