#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>
#include <optional>

class ModelStackWithAutoParam;

namespace deluge::edits {

/// M4a A/B toggle (default false): when true, SetParamValue::emitEvent drives the model-first
/// ParamSnapshot::applyPatch IN ADDITION to apply()'s in-line engine poke, so the host equivalence spec can compare
/// the two paths. The firmware/default build leaves it false — the in-line poke stays the sole path, so goldens are
/// bit-identical and there is zero RT change. Flipping it to the sole path (deleting the in-line poke) is the
/// deferred commit-flip, past M4a. Returns a reference so a test can set it; a function-local static keeps the
/// default centralised.
bool& m4EventPatchToggle();

/// Pilot Edit (M1b): set a param's value from a user knob-turn — the reification of the mutation
/// in View::modEncoderAction_existentParam. `pos`/`length` mirror the view's modPos/modLength
/// (length 0 = plain scalar turn; nonzero = held-pad region edit in automation view).
class SetParamValue final : public Edit {
public:
	SetParamValue(std::optional<project::ParamAddress> address, int32_t newValue, int32_t pos, int32_t length,
	              ModelStackWithAutoParam* engineStack, bool mayDeleteNodesInLinearRun = true)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length),
	      mayDeleteNodesInLinearRun_(mayDeleteNodesInLinearRun), engineStack_(engineStack) {}

	Error apply(Context& ctx) override;

	/// M4 event-feed pilot: publish the model-changed event keyed on `address_`. Additive/observational this
	/// slice — verifies the address resolves to a live DOM param (the feed's first real consumer).
	void emitEvent(Context& ctx) override;

private:
	/// Id-identity of the lane (nullopt = never-saved owner, engine-resolved only). Unused by the
	/// funnel-phase body; it is the durable name the M4 event feed and later model-first writes key on.
	std::optional<project::ParamAddress> address_;
	int32_t newValue_;
	int32_t pos_;
	int32_t length_;
	/// Engine-only node-pruning hint (not domain intent): forwarded verbatim to
	/// setValuePossiblyForRegion. MIDI/MPE sites pass false so a value that may be re-emitted as MIDI
	/// isn't pruned. Funnel-phase field — removed at the M3/M4 authority flip, like engineStack_.
	bool mayDeleteNodesInLinearRun_;
	/// Funnel-phase borrow (migration plan, module topology): the engine-side resolution the
	/// mutation still delegates to. Valid only for the duration of apply(); never stored beyond
	/// it. Retired when the engine consumes model events (M4) instead of being poked in-line.
	ModelStackWithAutoParam* engineStack_;
};

} // namespace deluge::edits
