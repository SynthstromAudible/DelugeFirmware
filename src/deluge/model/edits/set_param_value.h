#pragma once

#include "model/edits/edit.h"
#include "model/project_bridge/lane_target.h"
#include "project/param_address.h"
#include <cstdint>
#include <optional>

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
		LEARNED, ///< as WRITE (bridge::paramStackForWrite): the take written while the arrangement records and a
		         ///< missing cable made; no INSTRUMENT or DRUM owner marked edited, as a learned MIDI knob writes
	};

	/// @brief How the value is written.
	enum class Write : uint8_t {
		USER,  ///< a knob turn: undone and recorded as automation as the engine's setter does
		SETUP, ///< a default set up for the user: the value alone, as setCurrentValueWithNoReversionOrRecording sets
		       ///< it (no undo step, no automation); found as EXACT
	};

	/// @param address                   The param's address.
	/// @param newValue                  The value, as the engine stores it.
	/// @param pos                       The region's start, in ticks (a scalar turn: 0).
	/// @param length                    The region's length, in ticks (a scalar turn: 0).
	/// @param resolve                   How apply() finds the param.
	/// @param mayDeleteNodesInLinearRun Whether the engine may prune a node the write leaves in a straight line; MIDI
	///                                  and MPE writes pass false, as the value may be sent on as MIDI.
	/// @param write                     How the value is written.
	SetParamValue(project::ParamAddress address, int32_t newValue, int32_t pos = 0, int32_t length = 0,
	              Resolve resolve = Resolve::READ, bool mayDeleteNodesInLinearRun = true, Write write = Write::USER)
	    : address_(address), newValue_(newValue), pos_(pos), length_(length),
	      mayDeleteNodesInLinearRun_(mayDeleteNodesInLinearRun), resolve_(resolve), write_(write) {}

	/// @brief Write the value, then raise the reverb's recompute for a param the reverb's sidechain copies (a
	/// sidechain's shape, the song's sidechain volume).
	Error apply(Context& ctx) override;

	/// @brief Check that `address_` still resolves to a live DOM param (alpha/beta builds); publishes nothing.
	/// @param ctx Edit context.
	void emitEvent(Context& ctx) override;

private:
	/// @brief The write itself, on @p stack, the param apply() found.
	Error write(Context& ctx, const bridge::LaneTarget& lane, ModelStackWithAutoParam* stack);

	/// The lane's address. apply() writes model-first through it where it resolves in the project tree; emitEvent
	/// keys on it.
	project::ParamAddress address_;
	int32_t newValue_;
	int32_t pos_;
	int32_t length_;
	/// Engine-only node-pruning hint (not domain intent): forwarded to setValuePossiblyForRegion.
	bool mayDeleteNodesInLinearRun_;
	Resolve resolve_;
	Write write_;
};

/// @brief The lane of the param at @p address, found as @p resolve says; with WRITE or LEARNED, @p address moves to
/// the take the write lands in. Use it at once.
/// @param song    The song holding the owner.
/// @param address The param's address.
/// @param resolve How it is found.
/// @return The lane, or none where the song holds no such param.
std::optional<bridge::LaneTarget> lane_for(::Song& song, project::ParamAddress& address,
                                           SetParamValue::Resolve resolve);

} // namespace deluge::edits
