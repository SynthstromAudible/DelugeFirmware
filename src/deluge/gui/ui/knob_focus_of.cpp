#include "gui/ui/knob_focus_of.h"

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui {

namespace bind = deluge::scenes::bind;

namespace {

std::optional<project::ClipAddress> clipAddressOf(const ::Clip* clip) {
	return clip != nullptr ? std::optional{project::ClipAddress{clip->id()}} : std::nullopt;
}

/// The focus on @p output's own mod controllable, in @p clip (none for an arranger row).
bind::KnobFocus outputFocus(::Output& output, const ::Clip* clip) {
	if (!output.id()) {
		return {};
	}
	const uint32_t id = *output.id();
	const std::optional<project::ClipAddress> at = clipAddressOf(clip);
	switch (output.type) {
	case OutputType::SYNTH:
		return {.owner = bind::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, id}, .clip = at}};
	case OutputType::KIT:
		return {.owner = bind::ChainFocus{
		            .chain = {project::ChainAddress::Kind::OUTPUT, id}, .owner = bind::ChainOwner::KIT, .clip = at}};
	case OutputType::AUDIO:
		return {.owner = bind::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, id},
		                                  .owner = bind::ChainOwner::AUDIO_TRACK,
		                                  .clip = at}};
	case OutputType::MIDI_OUT:
		return {.owner = bind::MidiFocus{.track = project::TrackAddress{id}, .clip = at}};
	default: // a CV track's knobs move nothing
		return {};
	}
}

/// The focus on the sound drum @p drum of @p kit, in @p clip.
bind::KnobFocus drumFocus(::Kit& kit, ::SoundDrum& drum, const ::Clip* clip) {
	if (!drum.id() || !kit.id()) {
		return {};
	}
	bind::SoundFocus sound{.slot = {project::SlotAddress::Kind::DRUM, *drum.id()}, .kit = *kit.id()};
	if (clip != nullptr) {
		sound.clip = project::ClipAddress{clip->id()};
		sound.row = project::NoteRowAddress{clip->id(), true, static_cast<int32_t>(*drum.id())};
	}
	return {.owner = sound};
}

} // namespace

bind::KnobFocus knobFocusOf(::Song& song, const ModelStackWithThreeMainThings& stack) {
	bind::KnobFocus focus;
	ModControllable* controllable = stack.modControllable;
	if (controllable != nullptr && controllable != static_cast<ModControllable*>(&song.live.global_effectable)) {
		TimelineCounter* counter = stack.getTimelineCounterAllowNull();
		::Clip* clip = counter != nullptr && counter != static_cast<TimelineCounter*>(&song)
		                   ? static_cast<::Clip*>(counter)
		                   : nullptr;
		// The focus keeps the session clip a take records; the write lands in the take.
		if (clip != nullptr && clip->beingRecordedFromClip != nullptr) {
			clip = clip->beingRecordedFromClip;
		}
		for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
			if (output->toModControllable() == controllable) {
				focus = outputFocus(*output, clip);
				break;
			}
			if (output->type == OutputType::KIT) {
				auto& kit = static_cast<::Kit&>(*output);
				for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
					if (drum->type == DrumType::SOUND
					    && static_cast<ModControllable*>(static_cast<::SoundDrum*>(drum)) == controllable) {
						focus = drumFocus(kit, *static_cast<::SoundDrum*>(drum), clip);
						break;
					}
				}
				if (!std::holds_alternative<std::monostate>(focus.owner)) {
					break;
				}
			}
		}
	}
	else if (controllable != nullptr) {
		focus.owner = bind::SongFocus{};
	}
	return focus;
}

} // namespace deluge::gui
