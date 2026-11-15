#include "model/edits/set_mod_knob_mode.h"

#include "definitions_cxx.hpp"
#include "model/output.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetModKnobMode::apply(Context& ctx) {
	if (mode_ >= kNumModButtons) {
		return Error::UNSPECIFIED;
	}
	if (std::holds_alternative<project::SongAddress>(owner_)) {
		ctx.model.mod_knob_mode = mode_;
		return Error::NONE;
	}
	Output* output = deluge::bridge::outputWithId(ctx.song, std::get<project::TrackAddress>(owner_).output_id);
	if (output == nullptr) {
		return Error::UNSPECIFIED;
	}
	output->setModKnobMode(mode_); // the record and the engine field together
	return Error::NONE;
}

bool modKnobModeFor(KnobModeOwner owner, uint8_t mode) {
	if (currentSong == nullptr) {
		return false;
	}
	Context ctx{*currentSong, currentSong->project};
	SetModKnobMode edit{owner, mode};
	return deluge::edits::apply(ctx, edit) == Error::NONE;
}

} // namespace deluge::edits
