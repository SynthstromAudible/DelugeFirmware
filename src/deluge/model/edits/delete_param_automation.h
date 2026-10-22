#pragma once

#include "deluge-project/param_address.h"
#include "model/action/action_logger.h" // ActionAddition
#include "model/edits/edit.h"
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// @brief Clear a param's automation.
///
/// apply() opens the AUTOMATION_DELETE Action and delegates to AutoParam::deleteAutomation, which snapshots the
/// stolen before-nodes into that Action as the undo Consequence.
class DeleteParamAutomation final : public Edit {
public:
	/// @param address     Durable lane id; nullopt for a never-saved owner (engine-resolved). Not used by apply().
	/// @param engineStack Borrowed engine stack naming the auto-param; must outlive apply().
	/// @param addition    The Action-coalescing policy (tempo passes ALLOWED; others NOT_ALLOWED).
	DeleteParamAutomation(std::optional<project::ParamAddress> address, ModelStackWithAutoParam* engineStack,
	                      ActionAddition addition = ActionAddition::NOT_ALLOWED)
	    : address_(address), engineStack_(engineStack), addition_(addition) {}
	/// @brief The param at @p address, which apply() finds in the song itself (bridge::paramStackAt); an address that
	/// names no param is refused.
	/// @param address  The param's address.
	/// @param addition The Action-coalescing policy.
	explicit DeleteParamAutomation(project::ParamAddress address, ActionAddition addition = ActionAddition::NOT_ALLOWED)
	    : address_(address), engineStack_(nullptr), addition_(addition) {}

	/// @copydoc Edit::apply
	Error apply(Context& ctx) override;

private:
	/// Durable lane id (nullopt = never-saved owner, engine-resolved); apply() resolves it when no stack was given.
	std::optional<project::ParamAddress> address_;
	/// Borrowed engine stack, like SetParamValue::engineStack_; null for the address form.
	ModelStackWithAutoParam* engineStack_;
	/// The Action-coalescing policy (tempo passes ALLOWED; others NOT_ALLOWED).
	ActionAddition addition_;
};

} // namespace deluge::edits
