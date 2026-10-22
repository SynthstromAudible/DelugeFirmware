#include "model/edits/set_mod_knob_cc.h"

#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetModKnobCC::apply(Context& ctx) {
	Output* output = ctx.song.outputForId(track_.output_id);
	if (output == nullptr || output->type != OutputType::MIDI_OUT || output->docOutput_ == nullptr) {
		return Error::UNSPECIFIED;
	}
	auto& midi = *static_cast<MIDIInstrument*>(output);
	if (knob_ >= midi.modKnobCCAssignments.size()) {
		return Error::UNSPECIFIED;
	}
	std::vector<project::ModKnobCc>& ccs = output->docOutput_->mod_knob_ccs;
	if (ccs.size() <= knob_) {
		ccs.resize(knob_ + 1);
	}
	ccs[knob_] = cc_;
	midi.modKnobCCAssignments[knob_] = MIDIInstrument::engineCcOf(cc_);
	midi.editedByUser = true; // the track now saves its knobs
	return Error::NONE;
}

bool modKnobCCFor(project::TrackAddress track, uint8_t knob, project::ModKnobCc cc) {
	if (currentSong == nullptr) {
		return false;
	}
	Context ctx{*currentSong, currentSong->project};
	SetModKnobCC edit{track, knob, cc};
	return deluge::edits::apply(ctx, edit) == Error::NONE;
}

} // namespace deluge::edits
