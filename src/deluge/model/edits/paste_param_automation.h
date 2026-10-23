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
	/// @brief The param at @p address, which apply() finds in the song itself, making what it pastes into (a cable, a
	///        lane); with @p follow_take, while the arrangement records the clip, into its take (made once).
	/// @param address     The param's address.
	/// @param startPos    Start of the destination region, in ticks.
	/// @param endPos      End of the destination region, in ticks.
	/// @param scaleFactor Horizontal scale applied to the pasted automation.
	/// @param copied      The clipboard automation (borrowed).
	/// @param follow_take A gold knob's paste: the take while the arrangement records.
	PasteParamAutomation(project::ParamAddress address, int32_t startPos, int32_t endPos, float scaleFactor,
	                     CopiedParamAutomation* copied, bool follow_take = true)
	    : address_(address), startPos_(startPos), endPos_(endPos), scaleFactor_(scaleFactor), copied_(copied),
	      isPatchCable_(false), engineStack_(nullptr), follow_take_(follow_take) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	std::optional<project::ParamAddress> address_;
	int32_t startPos_;
	int32_t endPos_;
	float scaleFactor_;
	CopiedParamAutomation* copied_;        ///< Clipboard borrow.
	bool isPatchCable_;                    ///< Whether the destination is a patch cable; the address form's own.
	ModelStackWithAutoParam* engineStack_; ///< Engine borrow; null for the address form.
	bool follow_take_{false};              ///< The address form resolves as a knob's write does.
};

} // namespace deluge::edits
