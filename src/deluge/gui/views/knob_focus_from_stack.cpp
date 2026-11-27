#include "gui/views/knob_focus_from_stack.h"

#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui {

namespace {

std::optional<project::ClipAddress> clipAddressOf(const ::Clip* clip) {
	return clip != nullptr ? std::optional{project::ClipAddress{clip->id()}} : std::nullopt;
}

/// The focus on @p output's own params, in @p clip (none for an arranger row).
deluge::ui::controls::KnobFocus outputFocus(::Output& output, const ::Clip* clip) {
	if (!output.id()) {
		return {};
	}
	const uint32_t id = *output.id();
	const std::optional<project::ClipAddress> at = clipAddressOf(clip);
	switch (output.type) {
	case OutputType::SYNTH:
		return {.owner =
		            deluge::ui::controls::SoundFocus{.slot = {project::SlotAddress::Kind::OUTPUT, id}, .clip = at}};
	case OutputType::KIT:
		return {.owner = deluge::ui::controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, id},
		                                                  .owner = deluge::ui::controls::ChainOwner::KIT,
		                                                  .clip = at}};
	case OutputType::AUDIO:
		return {.owner = deluge::ui::controls::ChainFocus{.chain = {project::ChainAddress::Kind::OUTPUT, id},
		                                                  .owner = deluge::ui::controls::ChainOwner::AUDIO_TRACK,
		                                                  .clip = at}};
	case OutputType::MIDI_OUT:
		return {.owner = deluge::ui::controls::MidiFocus{.track = project::TrackAddress{id}, .clip = at}};
	case OutputType::CV:
		return {.owner =
		            deluge::ui::controls::NoControlsFocus{.kind = deluge::ui::controls::NoControlsFocus::Kind::CV_TRACK,
		                                                  .track = project::TrackAddress{id},
		                                                  .clip_id = at ? std::optional{at->clip_id} : std::nullopt}};
	default:
		return {};
	}
}

/// The focus on the sound drum @p drum of @p kit, in @p clip.
deluge::ui::controls::KnobFocus drumFocus(::Kit& kit, ::SoundDrum& drum, const ::Clip* clip) {
	if (!drum.id() || !kit.id()) {
		return {};
	}
	deluge::ui::controls::SoundFocus sound{.slot = {project::SlotAddress::Kind::DRUM, *drum.id()}, .kit = *kit.id()};
	if (clip != nullptr) {
		sound.clip = project::ClipAddress{clip->id()};
		sound.row = project::NoteRowAddress{clip->id(), true, static_cast<int32_t>(*drum.id())};
	}
	return {.owner = sound};
}

/// The focus on the MIDI or gate drum @p drum of @p kit, in @p clip: a drum with no knobs. A stack names such a drum
/// only when it is hand-built, as `InstrumentClip::getActiveModControllable` has no controllable for it.
deluge::ui::controls::KnobFocus nonAudioDrumFocus(::Kit& kit, const ::Drum& drum, const ::Clip* clip) {
	if (!drum.id() || !kit.id()) {
		return {};
	}
	const bool gate = drum.type == DrumType::GATE;
	return {.owner = deluge::ui::controls::NoControlsFocus{
	            .kind = gate ? deluge::ui::controls::NoControlsFocus::Kind::GATE_DRUM
	                         : deluge::ui::controls::NoControlsFocus::Kind::MIDI_DRUM,
	            .track = project::TrackAddress{*kit.id()},
	            .drum = project::KitRowAddress{*kit.id(), *drum.id()},
	            .clip_id = clip != nullptr ? std::optional{clip->id()} : std::nullopt}};
}

} // namespace

deluge::ui::controls::KnobFocus knobFocusFromStack(::Song& song, const ModelStackWithThreeMainThings& stack) {
	deluge::ui::controls::KnobFocus focus;
	const deluge::model::LaneOwner& owner = stack.owner;
	if (!deluge::model::is_none(owner) && !std::holds_alternative<deluge::model::MasterChannel*>(owner)) {
		TimelineCounter* counter = stack.getTimelineCounterAllowNull();
		::Clip* clip = counter != nullptr && counter != static_cast<TimelineCounter*>(&song)
		                   ? static_cast<::Clip*>(counter)
		                   : nullptr;
		// The focus keeps the session clip a take records; the write lands in the take.
		if (clip != nullptr && clip->beingRecordedFromClip != nullptr) {
			clip = clip->beingRecordedFromClip;
		}
		for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
			if (deluge::model::owner_of(*output) == owner) {
				focus = outputFocus(*output, clip);
				break;
			}
			if (output->type == OutputType::KIT) {
				auto& kit = static_cast<::Kit&>(*output);
				for (Drum* drum = kit.firstDrum; drum != nullptr; drum = drum->next) {
					if (drum->type == DrumType::SOUND && deluge::model::owner_of(*drum) == owner) {
						focus = drumFocus(kit, *static_cast<::SoundDrum*>(drum), clip);
						break;
					}
					// Reached from hand-built stacks only (see nonAudioDrumFocus).
					if (drum->type != DrumType::SOUND && deluge::model::owner_of(*drum) == owner) {
						focus = nonAudioDrumFocus(kit, *drum, clip);
						break;
					}
				}
				if (!std::holds_alternative<std::monostate>(focus.owner)) {
					break;
				}
			}
		}
	}
	else if (!deluge::model::is_none(owner)) {
		focus.owner = deluge::ui::controls::SongFocus{};
	}
	return focus;
}

deluge::ui::controls::KnobFocus knobFocusOfClip(::Clip& clip) {
	if (clip.output == nullptr) {
		return {};
	}
	if (clip.output->type != OutputType::KIT || static_cast<::InstrumentClip&>(clip).affectEntire) {
		return outputFocus(*clip.output, &clip);
	}
	// Without affect-entire a kit's knobs are its selected sound row's, which the clip has a note row for.
	auto& kit = static_cast<::Kit&>(*clip.output);
	::Drum* selected = kit.selectedDrum;
	if (selected == nullptr || selected->type != DrumType::SOUND
	    || static_cast<::InstrumentClip&>(clip).getNoteRowForDrum(selected) == nullptr) {
		return {};
	}
	return drumFocus(kit, *static_cast<::SoundDrum*>(selected), &clip);
}

} // namespace deluge::gui
