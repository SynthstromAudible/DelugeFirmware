#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithAutoParam;
class CopiedParamAutomation;

namespace deluge::edits {

/// @brief Paste clipboard automation into a param region.
///
/// apply() opens the AUTOMATION_PASTE Action, pre-snapshots the before-state, then delegates to AutoParam::paste.
class PasteParamAutomation final : public Edit {
public:
	/// @param address       Durable lane id (nullopt for a never-saved owner).
	/// @param startPos      Start of the destination region, in ticks.
	/// @param endPos        End of the destination region, in ticks.
	/// @param scaleFactor   Horizontal scale applied to the pasted automation.
	/// @param copied        The clipboard automation (borrowed).
	/// @param isPatchCable  Whether the destination param is a patch cable.
	/// @param engineStack   Borrowed engine stack naming the auto-param; must outlive apply().
	PasteParamAutomation(std::optional<project::ParamAddress> address, int32_t startPos, int32_t endPos,
	                     float scaleFactor, CopiedParamAutomation* copied, bool isPatchCable,
	                     ModelStackWithAutoParam* engineStack)
	    : address_(address), startPos_(startPos), endPos_(endPos), scaleFactor_(scaleFactor), copied_(copied),
	      isPatchCable_(isPatchCable), engineStack_(engineStack) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::ParamAddress> address_;
	int32_t startPos_;
	int32_t endPos_;
	float scaleFactor_;
	CopiedParamAutomation* copied_;        ///< Clipboard borrow.
	bool isPatchCable_;                    ///< Whether the destination is a patch cable.
	ModelStackWithAutoParam* engineStack_; ///< Engine borrow.
};

} // namespace deluge::edits
