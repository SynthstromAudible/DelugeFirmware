#include "model/edits/learn_mapping.h"

#include "io/debug/log.h"
#include "io/midi/learned_midi.h"
#include "io/midi/mapping_index.h"
#include "io/midi/midi_device.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/edits/bend_range_for.h"
#include "model/edits/set_patch_cable.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/note/note_row.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable.h"
#include "processing/sound/sound_drum.h"

#include <algorithm>
#include <optional>
#include <variant>

namespace deluge::edits {

namespace {

/// Bring the engine into line with the table @p ctx's model now holds, by @p projected (the projection's result); on
/// refusal put back @p before, the table as it was before the write.
Error project(Context& ctx, Error projected, deluge::project::MappingTable before) {
	if (projected != Error::NONE) {
		ctx.model.mappings = std::move(before);
		deluge::midi::rebuildMappingIndex(ctx.song);
		return projected;
	}
	deluge::midi::rebuildMappingIndex(ctx.song);
	return Error::NONE;
}

/// Apply @p edit to the current song, if there is one.
void applyToCurrentSong(Edit& edit) {
	if (currentSong == nullptr) {
		return;
	}
	Context ctx{*currentSong, currentSong->project};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		// A gesture names only what the song holds; a refusal here is a gesture and the song out of step.
		D_PRINTLN("mapping refused: its owner is not in the song");
	}
}

/// The current song's param target for @p param of @p owner, when @p owner holds controls of the current song.
std::optional<deluge::project::ParamTarget> knobTargetIn(::ModControllableAudio& owner, ParamDescriptor param) {
	if (currentSong == nullptr) {
		return std::nullopt;
	}
	const std::optional<deluge::project::ControlsAddress> address =
	    deluge::bridge::controlsAddressOf(owner, *currentSong);
	bool isSound = false;
	if (!address || deluge::bridge::controlsOwnerAt(*currentSong, *address, &isSound) != &owner) {
		return std::nullopt;
	}
	namespace params = deluge::modulation::params;
	return deluge::project::ParamTarget{
	    *address,
	    deluge::bridge::knobTargetOf(param, isSound ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL)};
}

/// The kit whose row @p drum is.
::Kit* kitOf(::Song& song, const ::Drum& drum) {
	for (::Output* list :
	     {song.live.media.first_output, static_cast<::Output*>(song.live.media.first_hibernating_instrument)}) {
		for (::Output* output = list; output != nullptr; output = output->next) {
			if (output->type == OutputType::KIT && static_cast<::Kit*>(output)->drumWithId(*drum.id()) == &drum) {
				return static_cast<::Kit*>(output);
			}
		}
	}
	return nullptr;
}

/// Every clip @p output plays in: its session clips, then its arrangement-only ones.
template <typename Visit>
void forEachClipOf(::Song& song, ::Output& output, Visit visit) {
	for (::Clip* clip : song.live.clips.session_clips) {
		if (clip->output == &output) {
			visit(*clip);
		}
	}
	for (int32_t i = 0; i < std::ssize(output.clipInstances); i++) {
		::Clip* clip = output.clipInstances[i].clip;
		if (clip != nullptr && clip->isArrangementOnlyClip()) {
			visit(*clip);
		}
	}
}

/// Make @p cable's velocity-to-level default the velocity-to-level cable of @p owner in @p clip.
void velocityCableFor(Context& ctx, project::OwnerId owner, ::Clip& clip, const ::MIDICable& cable) {
	ParamDescriptor volume;
	volume.setToHaveParamOnly(deluge::modulation::params::LOCAL_VOLUME);
	SetPatchCable write{{.owner = owner,
	                     .space = project::ParamSpace::ENGINE,
	                     .id = deluge::bridge::cable_depth(PatchSource::VELOCITY, volume),
	                     .clip_id = clip.id()},
	                    cable.defaultVelocityToLevel,
	                    PatchCable::getDefaultPolarity(PatchSource::VELOCITY),
	                    SetPatchCable::Write::SETUP};
	(void)deluge::edits::apply(ctx, write);
}

/// Is @p params' expression bend automated? An automated bend's range is the automation's to keep. A set made here
/// takes a drum's or a track's bend defaults, as @p forDrum says.
bool bendAutomated(::ParamManager& params, bool forDrum) {
	ExpressionParamSet* expression = params.getOrCreateExpressionParamSet(forDrum);
	return expression == nullptr || expression->params[0].isAutomated(*expression->docForParam(0));
}

/// A kit row's input learned from a note: the row's bend range from the note's channel or zone, and the cable's
/// velocity-to-level default on the row in every kit clip.
void laneInputLearned(Context& ctx, ::Kit& kit, ::Drum& drum, const InputLearn& input) {
	const ::MIDICable& cable = *input.cable;
	// A CC learned as a note names no channel or zone of the cable's, so it brings no bend range.
	const int32_t zone = input.channel_or_zone - MIDI_CHANNEL_MPE_LOWER_ZONE;
	const int32_t bend = input.channel_or_zone >= IS_A_CC ? 0
	                     : zone >= 0                      ? cable.mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL]
	                                                      : cable.inputChannels[input.channel_or_zone].bendRange;
	::Clip* current = ctx.song.getCurrentClip();
	if (bend != 0 && current != nullptr && current->type == ClipType::INSTRUMENT && current->output == &kit) {
		auto* clip = static_cast<::InstrumentClip*>(current);
		if (::NoteRow* row = clip->getNoteRowForDrum(&drum);
		    row != nullptr && !bendAutomated(row->paramManager, /*forDrum=*/true)) {
			bendRangeFor(clip, row->paramManager, BEND_RANGE_FINGER_LEVEL, bend);
		}
	}
	if (drum.type != DrumType::SOUND || cable.defaultVelocityToLevel == 0) {
		return;
	}
	forEachClipOf(ctx.song, kit, [&](::Clip& clip) {
		if (static_cast<::InstrumentClip&>(clip).getNoteRowForDrum(&drum) != nullptr) {
			velocityCableFor(ctx, project::OwnerId::drum(*drum.id()), clip, cable);
		}
	});
}

/// A track's input learned from a note: its bend ranges from the note's channel or zone, and, for a synth, the cable's
/// velocity-to-level default in every clip it plays in.
void trackInputLearned(Context& ctx, ::Instrument& instrument, const InputLearn& input) {
	const ::MIDICable& cable = *input.cable;
	// Null for a CV track with no clips.
	::ParamManager* params = instrument.getParamManager(&ctx.song);
	auto* active = static_cast<::InstrumentClip*>(instrument.getActiveClip());
	int32_t main = 0;
	if (input.channel_or_zone == MIDI_CHANNEL_MPE_LOWER_ZONE || input.channel_or_zone == MIDI_CHANNEL_MPE_UPPER_ZONE) {
		const int32_t zone = input.channel_or_zone - MIDI_CHANNEL_MPE_LOWER_ZONE;
		main = cable.mpeZoneBendRanges[zone][BEND_RANGE_MAIN];
		const int32_t finger = cable.mpeZoneBendRanges[zone][BEND_RANGE_FINGER_LEVEL];
		if (finger != 0 && (active == nullptr || !active->hasAnyPitchExpressionAutomationOnNoteRows())
		    && params != nullptr && params->getOrCreateExpressionParamSet() != nullptr) {
			bendRangeFor(active, *params, BEND_RANGE_FINGER_LEVEL, finger);
		}
	}
	else if (input.channel_or_zone < IS_A_CC) {
		main = cable.inputChannels[input.channel_or_zone].bendRange;
	}
	if (main != 0 && params != nullptr && !bendAutomated(*params, /*forDrum=*/false)) {
		bendRangeFor(active, *params, BEND_RANGE_MAIN, main);
	}
	if (instrument.type != OutputType::SYNTH || cable.defaultVelocityToLevel == 0) {
		return;
	}
	forEachClipOf(ctx.song, instrument, [&](::Clip& clip) {
		velocityCableFor(ctx, project::OwnerId::instrument(*instrument.id()), clip, cable);
	});
}

/// What learning or unlearning @p target's input brings: the instrument counts as edited, and a note learned brings
/// @p input's bend ranges and velocity cable.
void inputLearned(Context& ctx, const project::MappingTarget& target, const std::optional<InputLearn>& input) {
	if (const auto* track = std::get_if<project::TrackInputTarget>(&target)) {
		::Output* output = ctx.song.outputForId(track->output_id);
		if (output == nullptr || output->type == OutputType::AUDIO) {
			return;
		}
		auto& instrument = static_cast<::Instrument&>(*output);
		instrument.beenEdited(false);
		if (input && input->cable != nullptr) {
			trackInputLearned(ctx, instrument, *input);
		}
	}
	else if (const auto* lane = std::get_if<project::LaneInputTarget>(&target)) {
		::Drum* drum = deluge::bridge::drumWithId(ctx.song, lane->drum_id);
		::Kit* kit = drum != nullptr ? kitOf(ctx.song, *drum) : nullptr;
		if (kit == nullptr) {
			return;
		}
		kit->beenEdited(false);
		if (input && input->cable != nullptr) {
			laneInputLearned(ctx, *kit, *drum, *input);
		}
	}
}

} // namespace

Error LearnMapping::apply(Context& ctx) {
	deluge::bridge::applyPendingMappingOps(ctx.song); // a write the render made first lands first
	deluge::project::MappingTable before = ctx.model.mappings;
	const auto* param = std::get_if<deluge::project::ParamTarget>(&mapping_.target);
	const bool replacing = replaces_ && param != nullptr
	                       && std::ranges::any_of(before.entries(), [&](const deluge::project::Mapping& entry) {
		                          return entry.target == mapping_.target && entry.source == *replaces_;
	                          });
	if (replaces_) {
		ctx.model.mappings.relearn(*replaces_, mapping_);
	}
	else {
		ctx.model.mappings.learn(mapping_);
	}
	// A knob learned again in place keeps its takeover state, whatever else of it changed.
	const Error projected = replacing
	                            ? deluge::bridge::projectRelearnedKnob(ctx.song, *param, *replaces_, mapping_.source)
	                            : deluge::bridge::projectMapping(ctx.song, mapping_.target);
	const Error result = project(ctx, projected, std::move(before));
	if (result == Error::NONE) {
		inputLearned(ctx, mapping_.target, input_);
	}
	return result;
}

Error UnlearnMapping::apply(Context& ctx) {
	deluge::bridge::applyPendingMappingOps(ctx.song); // a write the render made first lands first
	deluge::project::MappingTable before = ctx.model.mappings;
	ctx.model.mappings.unlearn(target_);
	const Error result = project(ctx, deluge::bridge::projectMapping(ctx.song, target_), std::move(before));
	if (result == Error::NONE) {
		inputLearned(ctx, target_, std::nullopt);
	}
	return result;
}

Error learnedMappingIn(::Song& song, deluge::project::MappingTarget target, const ::LearnedMIDI& learned) {
	Context ctx{song, song.project};
	if (learned.containsSomething()) {
		LearnMapping edit{
		    deluge::project::Mapping{.source = deluge::bridge::command_of(learned), .target = std::move(target)}};
		return deluge::edits::apply(ctx, edit);
	}
	UnlearnMapping edit{std::move(target)};
	return deluge::edits::apply(ctx, edit);
}

void learnedMappingFor(deluge::project::MappingTarget target, const ::LearnedMIDI& learned) {
	if (currentSong != nullptr && learnedMappingIn(*currentSong, target, learned) != Error::NONE) {
		D_PRINTLN("mapping refused: its owner is not in the song");
	}
}

void learnMappingFor(deluge::project::MidiCommand source, deluge::project::MappingTarget target) {
	LearnMapping edit{deluge::project::Mapping{.source = std::move(source), .target = std::move(target)}};
	applyToCurrentSong(edit);
}

void unlearnMappingFor(deluge::project::MappingTarget target) {
	UnlearnMapping edit{std::move(target)};
	applyToCurrentSong(edit);
}

bool learnKnobFor(::ModControllableAudio& owner, ParamDescriptor param, ::MIDICable* cable, uint8_t noteOrCC,
                  uint8_t channel) {
	const std::optional<deluge::project::ParamTarget> target = knobTargetIn(owner, param);
	if (!target) {
		return owner.learnKnob(cable, param, noteOrCC, 0, channel, currentSong);
	}
	::LearnedMIDI learned;
	learned.cable = cable;
	learned.channelOrZone = channel;
	learned.noteOrCC = noteOrCC;
	deluge::bridge::applyPendingMappingOps(*currentSong); // the knob it replaces may be one the render wrote
	std::optional<deluge::project::MidiCommand> replaces;
	for (const deluge::project::Mapping& entry : currentSong->project.mappings.entries()) {
		if (cable != nullptr && entry.target == deluge::project::MappingTarget{*target}
		    && deluge::bridge::matches(entry.source, *cable, channel, noteOrCC)) {
			replaces = entry.source;
			break;
		}
	}
	Context ctx{*currentSong, currentSong->project};
	LearnMapping edit{deluge::project::Mapping{.source = deluge::bridge::command_of(learned), .target = *target},
	                  std::move(replaces)};
	return deluge::edits::apply(ctx, edit) == Error::NONE;
}

bool unlearnKnobsFor(::ModControllableAudio& owner, ParamDescriptor param) {
	const std::optional<deluge::project::ParamTarget> target = knobTargetIn(owner, param);
	if (!target) {
		return owner.unlearnKnobs(param, currentSong);
	}
	if (currentSong->project.mappings.find(*target) == nullptr) {
		return false;
	}
	Context ctx{*currentSong, currentSong->project};
	UnlearnMapping edit{*target};
	if (deluge::edits::apply(ctx, edit) != Error::NONE) {
		return false;
	}
	owner.ensureInaccessibleParamPresetValuesWithoutKnobsAreZero(currentSong);
	return true;
}

} // namespace deluge::edits
