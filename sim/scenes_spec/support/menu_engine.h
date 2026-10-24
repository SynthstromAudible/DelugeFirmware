#pragma once

// The engine objects a menu context addresses, resolved in its song -- what `SoundEditor::setup` resolves for the same
// visit. The scenes hold addresses only; a spec comparing against legacy items, or setting a fixture up through an
// engine object, resolves them here.

#include "model/project_bridge/controls_bridge.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "model/clip/audio_clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/non_audio_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"

namespace deluge::scenes::spec {

/// @brief What a menu context edits, as engine objects.
struct MenuEngine {
	Clip* clip = nullptr;                      ///< the clip the context addresses
	Output* output = nullptr;                  ///< the clip's output
	Sound* sound = nullptr;                    ///< the sound at the context's slot
	ParamManagerForTimeline* params = nullptr; ///< the clip's, a sound drum's note row's, or the song's
	ArpeggiatorSettings* arp = nullptr;        ///< the clip's, or the selected drum's
	Source* source = nullptr;                  ///< the sound's source at the context's index
	MultiRange* range = nullptr;               ///< the source's range at the context's index
	SampleControls* sample_controls = nullptr; ///< the source's, or the audio clip's
	ModControllableAudio* mod = nullptr;       ///< the sound, or the chain's owner (the song, a kit, an audio track)
};

/// @brief Resolve @p ctx's addresses in its song.
inline MenuEngine menu_engine(const deluge::scenes::menu::MenuContext& ctx) {
	MenuEngine e{};
	if (ctx.song == nullptr) {
		return e;
	}
	Song& song = *ctx.song;
	if (ctx.is_song_view) {
		e.params = &song.paramManager;
	}
	if (ctx.addresses.clip) {
		e.clip = song.clipForId(ctx.addresses.clip->clip_id);
	}
	auto* instrument_clip =
	    e.clip != nullptr && e.clip->type == ClipType::INSTRUMENT ? static_cast<InstrumentClip*>(e.clip) : nullptr;
	if (e.clip != nullptr) {
		e.output = e.clip->output;
		if (!ctx.is_song_view) {
			e.params = &e.clip->paramManager;
		}
		e.arp = instrument_clip != nullptr ? &instrument_clip->arpSettings : nullptr;
		if (ctx.is_audio_clip) {
			e.sample_controls = &static_cast<AudioClip*>(e.clip)->sampleControls;
		}
	}
	if (ctx.is_drum && e.output != nullptr && e.output->type == OutputType::KIT) {
		Drum* drum = static_cast<Kit*>(e.output)->selectedDrum;
		if (drum != nullptr && drum->type == DrumType::SOUND) {
			e.arp = &static_cast<SoundDrum*>(drum)->arpSettings;
			NoteRow* row = instrument_clip != nullptr ? instrument_clip->getNoteRowForDrum(drum) : nullptr;
			e.params = row != nullptr ? &row->paramManager : nullptr;
		}
		else if (drum != nullptr) {
			e.arp = &static_cast<NonAudioDrum*>(drum)->arpSettings;
			e.params = nullptr;
		}
	}
	if (ctx.addresses.chain) {
		e.mod = deluge::bridge::chainOwnerAt(*ctx.addresses.chain, song);
	}
	if (ctx.addresses.slot) {
		e.sound = deluge::bridge::soundAt(song, *ctx.addresses.slot);
		e.mod = e.sound;
		if (e.sound != nullptr) {
			e.source = &e.sound->sources[ctx.source_index];
			e.sample_controls = &e.source->sampleControls;
			if (ctx.range_index >= 0 && ctx.range_index < std::ssize(e.source->ranges)) {
				e.range = e.source->ranges.getElement(ctx.range_index);
			}
		}
	}
	return e;
}

/// @brief Whether the port's learn target @p port is legacy's learning thing @p legacy, on @p ctx's owner: the same
///        param by its engine id, or neither.
inline bool learns_as(const std::optional<deluge::project::ParamTarget>& port, ParamDescriptor legacy,
                      const deluge::scenes::menu::MenuContext& ctx) {
	if (legacy.isNull()) {
		return !port.has_value();
	}
	const auto unpatched = ctx.addresses.slot ? deluge::modulation::params::Kind::UNPATCHED_SOUND
	                                          : deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	return port && port->owner == deluge::scenes::menu::bindings::controls_owner(ctx)
	       && port->param == deluge::bridge::knobTargetOf(legacy, unpatched);
}

/// @brief Can the sound @p ctx edits hold note tails: `Sound::allowNoteTails` over the stack `SoundEditor::setup`
///        builds for it. No sound always can.
inline bool sound_allows_note_tails(const deluge::scenes::menu::MenuContext& ctx) {
	const MenuEngine e = menu_engine(ctx);
	if (e.sound == nullptr || e.clip == nullptr) {
		return true;
	}
	NoteRow* note_row = nullptr;
	int32_t note_row_index = 0;
	if (ctx.is_drum && e.clip->type == ClipType::INSTRUMENT) {
		note_row =
		    static_cast<InstrumentClip*>(e.clip)->getNoteRowForDrum(static_cast<SoundDrum*>(e.sound), &note_row_index);
	}
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithThreeMainThings* stack = setupModelStackWithThreeMainThingsIncludingNoteRow(
	    memory, ctx.song, e.clip, note_row_index, note_row, e.mod, e.params);
	return e.sound->allowNoteTails(stack->addSoundFlags(), /*disregardSampleLoop=*/true);
}

} // namespace deluge::scenes::spec
