#pragma once

#include "deluge-project/param_address.h"
#include "model/action/action_logger.h" // ActionAddition
#include "model/edits/edit.h"
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// Clear a param's automation — the reification of the AUTOMATION_DELETE call sites. apply() opens the
/// AUTOMATION_DELETE Action and delegates to AutoParam::deleteAutomation, which snapshots the stolen
/// before-nodes into that Action as the undo Consequence.
class DeleteParamAutomation final : public Edit {
public:
	DeleteParamAutomation(std::optional<project::ParamAddress> address, ModelStackWithAutoParam* engineStack,
	                      ActionAddition addition = ActionAddition::NOT_ALLOWED)
	    : address_(address), engineStack_(engineStack), addition_(addition) {}

	Error apply(Context& ctx) override;

private:
	/// Durable lane id (nullopt = never-saved owner, engine-resolved); unused by the funnel body.
	std::optional<project::ParamAddress> address_;
	/// Funnel-phase borrow, like SetParamValue::engineStack_. Retired at the M3/M4 flip.
	ModelStackWithAutoParam* engineStack_;
	/// Funnel-phase: the site's Action-coalescing policy (tempo passes ALLOWED; others NOT_ALLOWED).
	ActionAddition addition_;
};

} // namespace deluge::edits
