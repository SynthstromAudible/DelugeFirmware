#pragma once

// The engine objects a menu context addresses, resolved in the legacy song whose record it names -- what
// `SoundEditor::setup` resolves for the same visit. The scenes hold addresses only; a spec comparing against legacy
// items, or setting a fixture up through an engine object, resolves them here.

#include "model/project_bridge/controls_bridge.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"

#include "model/clip/instrument_clip.h"
#include "model/drum/non_audio_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/source_resources.h"

#include <cstdio>
#include <cstdlib>
#include <optional>

namespace deluge::ui::scenes::spec {

/// @brief What a menu context edits, as engine objects.
struct MenuEngine {
	Clip* clip = nullptr;                      ///< the clip the context addresses
	Output* output = nullptr;                  ///< the clip's output
	Sound* sound = nullptr;                    ///< the sound at the context's slot
	ParamManagerForTimeline* params = nullptr; ///< the clip's, a sound drum's note row's, or the song's
	ArpeggiatorSettings* arp = nullptr;        ///< the clip's, or the selected drum's
	SourceResources* source = nullptr;         ///< the sound's source at the context's index
	MultiRange* range = nullptr;               ///< the source's range at the context's index
	deluge::model::LaneOwner mod;              ///< the sound, or the chain's owner (the song, a kit, an audio track)
};

/// @brief Resolve @p ctx's addresses in @p song.
/// @param ctx  The menu's context.
/// @param song The legacy song whose record @p ctx names (`MenuContext::model`).
/// @return The engine objects; none when @p ctx names no record. A context over another song's record is a spec's
///         mispairing, which aborts with its reason on stderr rather than resolve to nothing.
inline MenuEngine menu_engine(const deluge::ui::scenes::menu::MenuContext& ctx, Song& song) {
	MenuEngine e{};
	if (ctx.model == nullptr) {
		return e;
	}
	if (ctx.model != &song.project) {
		std::fprintf(stderr, "menu_engine: the context names another song's record than the song passed\n");
		std::abort();
	}
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
		e.mod = deluge::bridge::chain_owner_at(*ctx.addresses.chain, song);
	}
	if (ctx.addresses.slot) {
		e.sound = deluge::bridge::soundAt(song, *ctx.addresses.slot);
		e.mod = e.sound != nullptr ? deluge::model::LaneOwner{e.sound} : deluge::model::LaneOwner{};
		if (e.sound != nullptr) {
			e.source = &e.sound->sourceResources[ctx.source_index];
			if (ctx.range_index >= 0 && ctx.range_index < std::ssize(e.source->ranges)) {
				e.range = e.source->ranges.getElement(ctx.range_index);
			}
		}
	}
	return e;
}

/// @brief Whether the port's learn target @p port is legacy's learning thing @p legacy, on @p ctx's owner: the same
///        param by its engine id, or neither.
inline bool learns_as(const std::optional<deluge::project::OwnerParamTarget>& port,
                      const deluge::modulation::KnobTarget& legacy, const deluge::ui::scenes::menu::MenuContext& ctx) {
	if (!legacy) {
		return !port.has_value();
	}
	const auto unpatched = ctx.addresses.slot ? deluge::modulation::params::Kind::UNPATCHED_SOUND
	                                          : deluge::modulation::params::Kind::UNPATCHED_GLOBAL;
	return port && port->owner == deluge::ui::scenes::menu::bindings::controls_owner(ctx)
	       && port->param.id == deluge::bridge::controlTargetOf(legacy, unpatched);
}

} // namespace deluge::ui::scenes::spec
