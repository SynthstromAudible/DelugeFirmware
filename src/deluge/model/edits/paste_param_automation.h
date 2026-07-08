#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithAutoParam;
class CopiedParamAutomation;

namespace deluge::edits {

/// Paste clipboard automation into a param region — the reification of pasteAutomation's mutation. apply()
/// opens the AUTOMATION_PASTE Action, pre-snapshots the before-state, then delegates to AutoParam::paste.
class PasteParamAutomation final : public Edit {
public:
	PasteParamAutomation(std::optional<project::ParamAddress> address, int32_t startPos, int32_t endPos,
	                     float scaleFactor, CopiedParamAutomation* copied, bool isPatchCable,
	                     ModelStackWithAutoParam* engineStack)
	    : address_(address), startPos_(startPos), endPos_(endPos), scaleFactor_(scaleFactor), copied_(copied),
	      isPatchCable_(isPatchCable), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	std::optional<project::ParamAddress> address_;
	int32_t startPos_;
	int32_t endPos_;
	float scaleFactor_;
	CopiedParamAutomation* copied_;        ///< Funnel-phase clipboard borrow.
	bool isPatchCable_;                    ///< Computed at the call site; carried verbatim.
	ModelStackWithAutoParam* engineStack_; ///< Funnel-phase borrow.
};

} // namespace deluge::edits
