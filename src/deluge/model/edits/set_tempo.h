#pragma once

#include "model/edits/edit.h"

namespace deluge::edits {

/// Set the song tempo from a samples-per-tick value (the reification of PlaybackHandler::grabTempoFromClip's
/// structural TEMPO_CHANGE change). apply() opens the TEMPO_CHANGE Action, applies the tempo via
/// setTempoFromNumSamples (with shouldLogAction=false, since we record the Consequence ourselves), and adds a
/// ConsequenceTempoChange(before→after). Song-global, so no address. The view keeps the grab guard, the
/// tick-time computation, and the display. (The ordinary tempo-knob VALUE edit is already funnelled by M1c-S5's
/// SetParamValue on UNPATCHED_TEMPO.)
class SetTempo final : public Edit {
public:
	explicit SetTempo(double timePerTick) : timePerTick_(timePerTick) {}

	Error apply(Context& ctx) override;

private:
	double timePerTick_;
};

} // namespace deluge::edits
