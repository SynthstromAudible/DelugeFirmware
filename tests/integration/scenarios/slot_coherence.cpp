/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "scenarios/slot_coherence.h"

#include "model/project_bridge/chain_bridge.h"
#include "model/song/song.h"

#include "effects/codec.h"
#include "effects/plugin.h"
#include "instruments/classic/codec.h"
#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/state_access.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/project_bridge/chain_bridge.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/mapping_bridge.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "project/instrument_slot.h"
#include "serialization/tree.h"

#include <cstdio>
#include <functional>

namespace deluge::harness {

namespace {

using deluge::serialization::Value;

/// @p record with what @p sound's runtime still holds of it in its place: each oscillator's type, DX7 patch and
/// ranges, and the effects the render plays. The rest the sound plays through its config, which the snapshot check
/// holds to the record; its routing's runtime copies the song-settings check holds to the routing's record.
deluge::instruments::classic::SoundState heldBy(const ::Sound& sound, deluge::instruments::classic::SoundState record) {
	deluge::instruments::classic::sound_effects(record) = sound.renderEffects();
	for (int32_t s = 0; s < kNumSources; s++) {
		const auto id = static_cast<deluge::instruments::classic::SourceId>(s);
		record =
		    deluge::instruments::classic::with_dx_patch(std::move(record), id, sound.sourceResources[s].heldDxPatch());
		record =
		    deluge::instruments::classic::with_ranges(std::move(record), id, sound.sourceResources[s].heldRanges());
	}
	return record;
}

/// Every sound of @p output: a synth's own, or a kit's sound drums.
void forEachSound(Output& output, const std::function<void(Sound&, const std::string&)>& visit) {
	if (output.type == OutputType::SYNTH) {
		visit(*static_cast<SoundInstrument*>(&output), "synth " + output.name);
	}
	else if (output.type == OutputType::KIT) {
		for (Drum* drum = static_cast<Kit*>(&output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND) {
				visit(*static_cast<SoundDrum*>(drum), "kit " + output.name + " drum " + drum->name());
			}
		}
	}
}

/// Whether @p owner, which a song holds, keeps its learned knobs in the song's table alone: none outside it.
bool knobsCoherent(deluge::model::LaneOwner owner) {
	return deluge::model::effects_owner_of(owner)->knobs->loaded.empty();
}

/// The kits', audio tracks' and the song's own learned knobs checked to be the song's table's alone, and the index to
/// play the table's knobs; each break printed. How many broke.
int32_t ownControlsIncoherent(::Song& song, const char* leg, const char* label) {
	int32_t failures = 0;
	auto check = [&](deluge::model::LaneOwner owner, const std::string& name) {
		const std::optional<deluge::project::ControlsAddress> address = deluge::bridge::controlsAddressOf(owner, song);
		if (address && !knobsCoherent(owner)) {
			std::printf("SLOT-COHERENT %s leg=%s %s: controls\n", label, leg, name.c_str());
			failures++;
		}
	};
	auto visit = [&](Output& output) {
		if (output.type == OutputType::KIT || output.type == OutputType::AUDIO) {
			check(deluge::model::owner_of(output), "output " + output.name);
		}
	};
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		visit(*output);
	}
	for (Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		visit(*sleeping);
	}
	check(&song.live.master_channel, "song");
	if (const std::string what = knobIndexIncoherence(song); !what.empty()) {
		std::printf("SLOT-COHERENT %s leg=%s %s\n", label, leg, what.c_str());
		failures++;
	}
	return failures;
}

} // namespace

std::optional<Value> encodedSlot(::Song& song, deluge::project::SlotAddress address) {
	const deluge::project::InstrumentSlot* slot = song.project.resolveSlot(address);
	if (slot == nullptr) {
		return std::nullopt;
	}
	return slot->state.encode({.input_tick_magnitude = slot->tick_magnitude});
}

std::string soundIncoherence(::Sound& sound, ::Song& song, CoherenceChecks checks) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address) {
		return {};
	}
	const deluge::instruments::classic::SoundState& record = sound.state();
	const deluge::project::InstrumentSlot* slot = song.project.resolveSlot(*address);
	if (slot == nullptr) {
		return "slot missing";
	}
	if (checks.runtime) {
		const deluge::instruments::classic::SoundState held = heldBy(sound, record);
		const std::string d = deluge::serialization::first_difference(deluge::instruments::classic::dump_state(held),
		                                                              deluge::instruments::classic::dump_state(record));
		if (!d.empty()) {
			return "runtime at " + d;
		}
		if (const deluge::effects::EffectStates& played = deluge::instruments::classic::sound_effects(held);
		    !(played == deluge::instruments::classic::sound_effects(record))) {
			const deluge::plugin::CodecContext context{.input_tick_magnitude = 1};
			return "runtime at effects "
			       + deluge::serialization::first_difference(
			           deluge::effects::encode(played, context),
			           deluge::effects::encode(deluge::instruments::classic::sound_effects(record), context));
		}
	}
	if (checks.bound && slot->state.as<deluge::instruments::classic::DelugeSynth>() != &record) {
		return "bound: the sound plays a copy of its slot's state";
	}
	if (checks.controls) {
		const std::optional<deluge::project::ControlsAddress> owner = deluge::bridge::controlsAddressOf(sound);
		const std::vector<deluge::project::ControllerMapping>* controls =
		    owner ? song.project.resolveControls(*owner) : nullptr;
		if (controls == nullptr || *controls != deluge::bridge::controlsOf(sound)) {
			return "controls";
		}
		if (!knobsCoherent(&sound)) {
			return "learned knobs";
		}
	}
	if (checks.decode) {
		const deluge::plugin::CodecContext context{.input_tick_magnitude = slot->tick_magnitude};
		const auto decoded = deluge::instruments::classic::decode_onto(
		    deluge::instruments::classic::encode(record, context), record, context);
		if (!decoded) {
			return "decode refused";
		}
		if (!(deluge::instruments::classic::SoundState(*decoded) == record)) {
			return "decode at "
			       + deluge::serialization::first_difference(deluge::instruments::classic::dump_state(*decoded),
			                                                 deluge::instruments::classic::dump_state(record));
		}
	}
	if (checks.snapshot && !(sound.config() == deluge::instruments::classic::DelugeSynth::derive(record))) {
		return "config"; // the sound plays what its record derives
	}
	return {};
}

namespace {

/// Where @p owner, playing @p record, is out of step with it, or empty: it plays the record's effects and view (bound),
/// the record's effects decode to what they encode (decode), and the render's copy is what it plays.
std::string chainDifference(const deluge::model::EffectsOwner& owner, const deluge::project::FxChain& record,
                            int32_t tick_magnitude) {
	const deluge::effects::EffectStates* held = record.effects.as<deluge::effects::DelugeEffects>();
	if (owner.view->bound() != &record || held != &owner.effects()) {
		return "the owner does not play its record";
	}
	const deluge::plugin::CodecContext context{.input_tick_magnitude = tick_magnitude};
	const deluge::serialization::Value encoded = record.effects.encode(context);
	deluge::plugin::StateHandle decoded = deluge::plugin::StateHandle::of<deluge::effects::DelugeEffects>({});
	if (!decoded.decode(encoded, context)) {
		return "decode refused";
	}
	if (const std::string d = deluge::serialization::first_difference(decoded.encode(context), encoded); !d.empty()) {
		return "decode at " + d;
	}
	if (owner.binding->renderEffects() != owner.effects()) {
		return "render copy";
	}
	if (owner.binding->renderModFXParam() != owner.view->modFXParam()) {
		return "render copy's mod FX knob";
	}
	return {};
}

} // namespace

std::string chainIncoherence(::Song& song) {
	for (const Output* output : std::initializer_list<const Output*>{song.live.media.first_output,
	                                                                 song.live.media.first_hibernating_instrument}) {
		for (; output != nullptr; output = output->next) {
			if ((output->type != OutputType::KIT && output->type != OutputType::AUDIO) || !output->id()) {
				continue;
			}
			const deluge::project::DocOutput* record = song.project.output_structure.find(*output->id());
			if (record == nullptr) {
				continue;
			}
			const std::string where = "output " + std::to_string(*output->id()) + " effects: ";
			if (!record->effects) {
				return where + "no chain";
			}
			const std::optional<deluge::model::EffectsOwner> owner = deluge::bridge::chainAt(
			    {.kind = deluge::project::ChainAddress::Kind::OUTPUT, .id = *output->id()}, song);
			if (!owner) {
				return where + "no owner";
			}
			if (const std::string d = chainDifference(*owner, *record->effects, song.getInputTickMagnitude());
			    !d.empty()) {
				return where + d;
			}
		}
	}
	if (const std::string d = chainDifference(*song.live.master_channel.effectsOwner(), song.project.master_effects,
	                                          song.getInputTickMagnitude());
	    !d.empty()) {
		return "master effects: " + d;
	}
	return {};
}

int32_t expectSongCoherent(::Song& song, const char* leg, CoherenceChecks checks) {
	const char* label = checks.expect_incoherent ? "EXPECTED-INCOHERENT" : "FAIL";
	int32_t failures = 0;
	auto check = [&](Sound& sound, const std::string& owner) {
		if (const std::string what = soundIncoherence(sound, song, checks); !what.empty()) {
			std::printf("SLOT-COHERENT %s leg=%s %s: %s\n", label, leg, owner.c_str(), what.c_str());
			failures++;
		}
	};
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		forEachSound(*output, check);
	}
	for (Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		forEachSound(*sleeping, check);
	}
	if (checks.controls) {
		failures += ownControlsIncoherent(song, leg, label);
	}
	if (checks.chains) {
		if (const std::string what = chainIncoherence(song); !what.empty()) {
			std::printf("SLOT-COHERENT %s leg=%s %s\n", label, leg, what.c_str());
			failures++;
		}
	}
	if (checks.song_settings) {
		if (const std::string what = songSettingsIncoherence(song); !what.empty()) {
			std::printf("SLOT-COHERENT %s leg=%s song-settings: %s\n", label, leg, what.c_str());
			failures++;
		}
	}
	return failures;
}

} // namespace deluge::harness
