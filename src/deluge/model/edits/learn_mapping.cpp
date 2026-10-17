#include "model/edits/learn_mapping.h"

#include "io/debug/log.h"
#include "io/midi/learned_midi.h"
#include "io/midi/mapping_index.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/midi_command.h"
#include "model/song/song.h"

#include <optional>

namespace deluge::edits {

namespace {

/// Project @p target from the table @p ctx's model now holds; on refusal put back @p before, the table as it was
/// before the write.
Error project(Context& ctx, const deluge::project::MappingTarget& target, deluge::project::MappingTable before) {
	const Error projected = deluge::bridge::projectMapping(ctx.song, target);
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

} // namespace

Error LearnMapping::apply(Context& ctx) {
	deluge::bridge::applyPendingMappingOps(ctx.song); // a write the render made first lands first
	deluge::project::MappingTable before = ctx.model.mappings;
	ctx.model.mappings.learn(mapping_);
	return project(ctx, mapping_.target, std::move(before));
}

Error UnlearnMapping::apply(Context& ctx) {
	deluge::bridge::applyPendingMappingOps(ctx.song); // a write the render made first lands first
	deluge::project::MappingTable before = ctx.model.mappings;
	ctx.model.mappings.unlearn(target_);
	return project(ctx, target_, std::move(before));
}

Error learnedMappingIn(::Song& song, deluge::project::MappingTarget target, const ::LearnedMIDI& learned) {
	Context ctx{song, song.project};
	if (learned.containsSomething()) {
		LearnMapping edit{deluge::project::Mapping{.source = deluge::bridge::command_of(learned), .target = target}};
		return deluge::edits::apply(ctx, edit);
	}
	UnlearnMapping edit{target};
	return deluge::edits::apply(ctx, edit);
}

void learnedMappingFor(deluge::project::MappingTarget target, const ::LearnedMIDI& learned) {
	if (currentSong != nullptr && learnedMappingIn(*currentSong, target, learned) != Error::NONE) {
		D_PRINTLN("mapping refused: its owner is not in the song");
	}
}

void learnMappingFor(deluge::project::MidiCommand source, deluge::project::MappingTarget target) {
	LearnMapping edit{deluge::project::Mapping{.source = std::move(source), .target = target}};
	applyToCurrentSong(edit);
}

void unlearnMappingFor(deluge::project::MappingTarget target) {
	UnlearnMapping edit{target};
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
	Context ctx{*currentSong, currentSong->project};
	LearnMapping edit{deluge::project::Mapping{.source = deluge::bridge::command_of(learned), .target = *target}};
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
