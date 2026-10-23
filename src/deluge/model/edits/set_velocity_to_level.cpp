#include "model/edits/set_velocity_to_level.h"

#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/learn_mapping.h"
#include "model/instrument/kit.h"
#include "model/song/clip_iterators.h"
#include "model/song/song.h"
#include "processing/sound/sound_instrument.h"

namespace deluge::edits {

Error SetVelocityToLevelEverywhere::apply(Context& ctx) {
	// `Song::grabVelocityToLevelFromMIDICableAndSetupPatchingForEverything`: every clip, everywhere it plays.
	for (::Clip* clip : AllClips::everywhere(&ctx.song)) {
		::Output* output = clip->output;
		if (output == nullptr || !output->id()) {
			continue;
		}
		if (output->type == OutputType::SYNTH) {
			const auto& input = static_cast<::SoundInstrument*>(output)->midiInput;
			if (input.containsSomething() && input.cable == &cable_) {
				velocityCableFor(ctx, project::OwnerId::instrument(*output->id()), *clip, cable_);
			}
		}
		else if (output->type == OutputType::KIT) {
			for (::Drum* drum = static_cast<::Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type == DrumType::SOUND && drum->id() && drum->midiInput.containsSomething()
				    && drum->midiInput.cable == &cable_
				    && static_cast<::InstrumentClip*>(clip)->getNoteRowForDrum(drum) != nullptr) {
					velocityCableFor(ctx, project::OwnerId::drum(*drum->id()), *clip, cable_);
				}
			}
		}
	}
	return Error::NONE;
}

} // namespace deluge::edits
