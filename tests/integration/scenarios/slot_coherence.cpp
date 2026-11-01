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

#include "deluge-project/instrument_slot.h"
#include "effects/codec.h"
#include "effects/plugin.h"
#include "instruments/deluge/codec.h"
#include "instruments/deluge/plugin.h"
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
#include "serialization/tree.h"

#include <cstdio>
#include <functional>

namespace deluge::harness {

namespace {

using deluge::serialization::Value;

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

/// Whether the song's table holds @p owner's learned knobs as the runtime's @p knobs are.
bool knobsCoherent(deluge::project::ControlsAddress owner, const ::ModControllableAudio& knobs, bool isSound,
                   ::Song& song) {
	return song.project.mappings.params_of(owner) == deluge::bridge::knobsOf(owner, knobs, isSound);
}

/// The kits', audio tracks' and the song's own learned knobs checked against the song's table; each break printed.
/// How many broke.
int32_t ownControlsIncoherent(::Song& song, const char* leg, const char* label) {
	int32_t failures = 0;
	auto check = [&](::ModControllableAudio& owner, const std::string& name) {
		const std::optional<deluge::project::ControlsAddress> address = deluge::bridge::controlsAddressOf(owner, song);
		if (address && !knobsCoherent(*address, owner, false, song)) {
			std::printf("SLOT-COHERENT %s leg=%s %s: controls\n", label, leg, name.c_str());
			failures++;
		}
	};
	auto visit = [&](Output& output) {
		if (output.type == OutputType::KIT || output.type == OutputType::AUDIO) {
			if (auto* own = static_cast<::ModControllableAudio*>(output.toModControllable())) {
				check(*own, "output " + output.name);
			}
		}
	};
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		visit(*output);
	}
	for (Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		visit(*sleeping);
	}
	check(song.live.global_effectable, "song");
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
	if (!address || sound.boundState() == nullptr) {
		return {};
	}
	const deluge::instrument::SoundState& record = *sound.boundState();
	const deluge::project::InstrumentSlot* slot = song.project.resolveSlot(*address);
	if (slot == nullptr) {
		return "slot missing";
	}
	if (checks.runtime) {
		const std::string d = deluge::serialization::first_difference(
		    deluge::instrument::dump_state(sound.seededState()), deluge::instrument::dump_state(record));
		if (!d.empty()) {
			return "runtime at " + d;
		}
		if (const deluge::effects::EffectStates played = sound.seededState().effects; !(played == record.effects)) {
			const deluge::plugin::CodecContext context{.input_tick_magnitude = 1};
			return "runtime at effects "
			       + deluge::serialization::first_difference(deluge::effects::encode(played, context),
			                                                 deluge::effects::encode(record.effects, context));
		}
	}
	if (checks.bound && slot->state.as<deluge::instrument::DelugeInstrument>() != &record) {
		return "bound: the sound plays a copy of its slot's state";
	}
	if (checks.controls) {
		const std::optional<deluge::project::ControlsAddress> owner = deluge::bridge::controlsAddressOf(sound);
		const std::vector<deluge::project::ControllerMapping>* controls =
		    owner ? song.project.resolveControls(*owner) : nullptr;
		if (controls == nullptr || *controls != deluge::bridge::controlsOf(sound)) {
			return "controls";
		}
		if (!knobsCoherent(*owner, sound, true, song)) {
			return "learned knobs";
		}
	}
	if (checks.decode) {
		const deluge::plugin::CodecContext context{.input_tick_magnitude = slot->tick_magnitude};
		const auto decoded =
		    deluge::instrument::decode_onto(deluge::instrument::encode(record, context), record, context);
		if (!decoded) {
			return "decode refused";
		}
		if (!(*decoded == record)) {
			return "decode at "
			       + deluge::serialization::first_difference(deluge::instrument::dump_state(*decoded),
			                                                 deluge::instrument::dump_state(record));
		}
	}
	if (checks.snapshot) {
		const deluge::synth::SoundSnapshot before = sound.snapshot();
		sound.projectSnapshot();
		if (!(sound.snapshot() == before)) {
			return "snapshot";
		}
	}
	return {};
}

namespace {

/// Where @p owner, playing @p record, is out of step with it, or empty: it plays the record's effects and view (bound),
/// the record's effects decode to what they encode (decode), and the render's copy is what it plays.
std::string chainDifference(const ::GlobalEffectable& owner, const deluge::project::FxChain& record,
                            int32_t tick_magnitude) {
	const deluge::effects::EffectStates* held = record.effects.as<deluge::effects::DelugeEffects>();
	if (owner.boundChain() != &record || held != &owner.effects()) {
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
	if (owner.modControllableSnapshot().effects != owner.effects()) {
		return "render copy";
	}
	if (owner.modControllableSnapshot().mod_fx_param != owner.modFXParam()) {
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
			const ::GlobalEffectable* owner = deluge::bridge::chainOwnerAt(
			    {.kind = deluge::project::ChainAddress::Kind::OUTPUT, .id = *output->id()}, song);
			if (owner == nullptr) {
				return where + "no owner";
			}
			if (const std::string d = chainDifference(*owner, *record->effects, song.getInputTickMagnitude());
			    !d.empty()) {
				return where + d;
			}
		}
	}
	if (const std::string d =
	        chainDifference(song.live.global_effectable, song.project.master_effects, song.getInputTickMagnitude());
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
