#include "model/edits/move_mod_knob_cc.h"

#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"

namespace deluge::edits {

Error MoveModKnobCC::apply(Context& ctx) {
	Output* output = ctx.song.outputForId(track_.output_id);
	if (output == nullptr || output->type != OutputType::MIDI_OUT || output->docOutput_ == nullptr) {
		return Error::UNSPECIFIED;
	}
	auto& midi = *static_cast<MIDIInstrument*>(output);
	if (knob_ >= midi.modKnobCCAssignments.size()) {
		return Error::UNSPECIFIED;
	}
	// A knob that sends nothing stays so: legacy's move reports it as it is.
	if (midi.modKnobCCAssignments[knob_] >= CC_NUMBER_NONE) {
		moved_to_ = midi.modKnobCCAssignments[knob_];
		return Error::NONE;
	}
	const int32_t moved = midi.moveKnobCCAutomation(knob_, offset_, ctx.song);
	if (moved < 0) {
		return Error::UNSPECIFIED;
	}
	midi.markEdited(); // its record holds every knob
	output->docOutput_->mod_knob_ccs[knob_] = MIDIInstrument::modKnobCcOf(static_cast<int8_t>(moved));
	moved_to_ = moved;
	return Error::NONE;
}

std::optional<int32_t> moveModKnobCCFor(project::TrackAddress track, uint8_t knob, int32_t offset) {
	if (currentSong == nullptr) {
		return std::nullopt;
	}
	Context ctx{*currentSong, currentSong->project};
	MoveModKnobCC edit{track, knob, offset};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		return std::nullopt;
	}
	return edit.moved_to();
}

} // namespace deluge::edits
