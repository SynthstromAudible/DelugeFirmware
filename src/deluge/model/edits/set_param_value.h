#pragma once

#include "deluge-project/param_address.h"
#include "model/edits/edit.h"
#include <cstdint>

class ModelStackWithAutoParam;
class Song;

namespace deluge::edits {

/// @brief Set a param's value from a user knob-turn.
///
/// `pos`/`length` select the region: length 0 is a plain scalar turn; nonzero is a held-pad region edit in
/// automation view. apply() finds the param at the address itself, below the Edit, as `Resolve` says.
class SetParamValue final : public Edit {
public:
	/// @brief How apply() finds its param.
	enum class Resolve : uint8_t {
		READ,  ///< as it stands (bridge::paramStackAt): an address that names no param is refused
		WRITE, ///< as a knob writes it (bridge::paramStackForWrite): what it turns is made, the take written while
		       ///< the arrangement records, and an INSTRUMENT or DRUM owner marked edited
		EXACT, ///< the param the address names exactly (bridge::paramStackExact): the lane a caller's own engine stack
		       ///< names, whose address it passes
	};

	/// @param address                   The param's address.
	/// @param newValue                  The value, as the engine stores it.
	/// @param pos                       The region's start, in ticks (a scalar turn: 0).
	/// @param length                    The region's length, in ticks (a scalar turn: 0).
	/// @param resolve                   How apply() finds the param.
	/// @param mayDeleteNodesInLinearRun Whether the engine may prune a node the write leaves in a straight line; MIDI
	///                                  and MPE writes pass false, as the value may be sent on as MIDI.
	SetParamValue(project::ParamAddress address, int32_t newValue, int32_t pos = 0, int32_t length = 0,
	              Resolve resolve = Resolve::READ, bool mayDeleteNodesInLinearRun = true)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length),
	      mayDeleteNodesInLinearRun_(mayDeleteNodesInLinearRun), resolve_(resolve) {}

	/// @brief Write the value, then raise the reverb's recompute for a param the reverb's sidechain copies (a
	/// sidechain's shape, the song's sidechain volume).
	Error apply(Context& ctx) override;

	/// @brief Check that `address_` still resolves to a live DOM param (alpha/beta builds); publishes nothing.
	/// @param ctx Edit context.
	void emitEvent(Context& ctx) override;

private:
	/// @brief The write itself, on @p stack, the param apply() found.
	Error write(Context& ctx, ModelStackWithAutoParam* stack);

	/// The lane's address. apply() writes model-first through it where it resolves in the project tree; emitEvent
	/// keys on it.
	project::ParamAddress address_;
	int32_t newValue_;
	int32_t pos_;
	int32_t length_;
	/// Engine-only node-pruning hint (not domain intent): forwarded to setValuePossiblyForRegion.
	bool mayDeleteNodesInLinearRun_;
	Resolve resolve_;
};

/// @brief The engine stack of the param at @p address, found as @p resolve says; with WRITE, @p address moves to the
/// take the write lands in.
/// @param song    The song holding the owner.
/// @param address The param's address.
/// @param resolve How it is found.
/// @param memory  MODEL_STACK_MAX_SIZE bytes, aligned as ModelStackWithAutoParam, which the stack lives in.
/// @return The stack, or null where the song holds no such param.
ModelStackWithAutoParam* paramStackFor(::Song& song, project::ParamAddress& address, SetParamValue::Resolve resolve,
                                       void* memory);

} // namespace deluge::edits
