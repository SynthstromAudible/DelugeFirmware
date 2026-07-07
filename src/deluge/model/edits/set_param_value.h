#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// Pilot Edit (M1b): set a param's value from a user knob-turn — the reification of the mutation
/// in View::modEncoderAction_existentParam. `pos`/`length` mirror the view's modPos/modLength
/// (length 0 = plain scalar turn; nonzero = held-pad region edit in automation view).
class SetParamValue final : public Edit {
public:
	SetParamValue(std::optional<project::ParamAddress> address, int32_t newValue, int32_t pos, int32_t length,
	              ModelStackWithAutoParam* engineStack)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

private:
	/// Id-identity of the lane (nullopt = never-saved owner, engine-resolved only). Unused by the
	/// funnel-phase body; it is the durable name the M4 event feed and later model-first writes key on.
	std::optional<project::ParamAddress> address_;
	int32_t newValue_;
	int32_t pos_;
	int32_t length_;
	/// Funnel-phase borrow (migration plan, module topology): the engine-side resolution the
	/// mutation still delegates to. Valid only for the duration of apply(); never stored beyond
	/// it. Retired when the engine consumes model events (M4) instead of being poked in-line.
	ModelStackWithAutoParam* engineStack_;
};

} // namespace deluge::edits
