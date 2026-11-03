#pragma once

#include "deluge-project/param_address.h"
#include "model/action/action_logger.h" // ActionAddition
#include "model/edits/edit.h"
#include "model/edits/set_param_value.h"

namespace deluge::edits {

/// @brief Clear a param's automation.
///
/// apply() opens the AUTOMATION_DELETE Action and delegates to AutoParam::deleteAutomation, which snapshots the
/// stolen before-nodes into that Action as the undo Consequence.
class DeleteParamAutomation final : public Edit {
public:
	/// @brief The param at @p address, which apply() finds in the song itself as @p resolve says (an address that
	/// names no param is refused); a knob's delete resolves as a write, the take's while the arrangement records.
	/// @param address  The param's address.
	/// @param addition The Action-coalescing policy (tempo passes ALLOWED; others NOT_ALLOWED).
	/// @param resolve  How apply() finds the param.
	explicit DeleteParamAutomation(project::ParamAddress address, ActionAddition addition = ActionAddition::NOT_ALLOWED,
	                               SetParamValue::Resolve resolve = SetParamValue::Resolve::READ)
	    : address_(address), addition_(addition), resolve_(resolve) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	project::ParamAddress address_;
	/// The Action-coalescing policy (tempo passes ALLOWED; others NOT_ALLOWED).
	ActionAddition addition_;
	SetParamValue::Resolve resolve_;
};

} // namespace deluge::edits
