#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// @brief Set a param's value from a user knob-turn.
///
/// `pos`/`length` select the region: length 0 is a plain scalar turn; nonzero is a held-pad region edit in
/// automation view.
class SetParamValue final : public Edit {
public:
	SetParamValue(std::optional<project::ParamAddress> address, int32_t newValue, int32_t pos, int32_t length,
	              ModelStackWithAutoParam* engineStack, bool mayDeleteNodesInLinearRun = true)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length),
	      mayDeleteNodesInLinearRun_(mayDeleteNodesInLinearRun), engineStack_(engineStack) {}
	/// @brief The param at @p address, which apply() finds in the song itself (bridge::paramStackAt); an address that
	/// names no param is refused.
	SetParamValue(project::ParamAddress address, int32_t newValue, int32_t pos = 0, int32_t length = 0)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length), mayDeleteNodesInLinearRun_(true),
	      engineStack_(nullptr) {}

	/// @brief Write the value, then raise the reverb's recompute for a param the reverb's sidechain copies (a
	/// sidechain's shape, the song's sidechain volume).
	Error apply(Context& ctx) override;

	/// @brief Check that `address_` still resolves to a live DOM param (alpha/beta builds); publishes nothing.
	/// @param ctx Edit context.
	void emitEvent(Context& ctx) override;

private:
	/// @brief The write itself, on engineStack_.
	Error write(Context& ctx);

	/// Id-identity of the lane (nullopt = never-saved owner, engine-resolved only). apply() writes model-first through
	/// it when it resolves; emitEvent keys on it.
	std::optional<project::ParamAddress> address_;
	int32_t newValue_;
	int32_t pos_;
	int32_t length_;
	/// Engine-only node-pruning hint (not domain intent): forwarded to setValuePossiblyForRegion. MIDI/MPE sites
	/// pass false so a value that may be re-emitted as MIDI isn't pruned.
	bool mayDeleteNodesInLinearRun_;
	/// Engine-side resolution the mutation delegates to: the caller's, or, for the address form, the stack apply()
	/// resolves in its own frame. Valid only for the duration of apply(); never stored beyond it.
	ModelStackWithAutoParam* engineStack_;
};

} // namespace deluge::edits
