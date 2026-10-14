#pragma once

#include "model/edits/edit.h"

namespace deluge::edits {

/// @brief Set the song tempo from a samples-per-tick value (the structural TEMPO_CHANGE change).
///
/// apply() opens the TEMPO_CHANGE Action, applies the tempo via setTempoFromNumSamples (with shouldLogAction=false,
/// since it records the Consequence itself), and adds a ConsequenceTempoChange(before→after). Song-global, so no
/// address. The caller owns the grab guard, the tick-time computation, and the display. The ordinary tempo-knob
/// value edit goes through SetParamValue on UNPATCHED_TEMPO.
class SetTempo final : public Edit {
public:
	explicit SetTempo(double timePerTick) : timePerTick_(timePerTick) {}

	Error apply(Context& ctx) override;

private:
	double timePerTick_;
};

} // namespace deluge::edits
