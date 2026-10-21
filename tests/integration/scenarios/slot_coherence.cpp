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
#include "engines/deluge/codec.h"
#include "engines/effects/codec.h"
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

/// The first instrument entry where @p slot and @p encoded differ, by key: one holds a key the other lacks, or the two
/// values differ, a scalar's kind (number or text) included, since a commit re-encodes only the entries it touches and
/// keeps every other one as the slot holds it. Empty when they agree. The order of the top-level keys is not compared:
/// a slot keeps a file's order, and appends what the file lacked.
std::string stateDifference(const Value& slot, const Value& encoded) {
	for (const auto& [key, value] : encoded.entries()) {
		const Value* held = slot.find(key);
		if (held == nullptr) {
			return key + " (missing)";
		}
		if (std::string d = deluge::serialization::first_difference(*held, value, key); !d.empty()) {
			return d;
		}
		if (!(*held == value)) {
			return key + " (a scalar's kind or an entry's order)";
		}
	}
	for (const auto& [key, value] : slot.entries()) {
		if (deluge::instrument::is_state_key(key) && encoded.find(key) == nullptr) {
			return key + " (not encoded)";
		}
	}
	return {};
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

std::string soundIncoherence(::Sound& sound, ::Song& song, CoherenceChecks checks) {
	const std::optional<deluge::project::SlotAddress> address = deluge::bridge::slotAddressOf(sound);
	if (!address || sound.boundState() == nullptr) {
		return {};
	}
	const deluge::instrument::SoundState& record = *sound.boundState();
	if (checks.runtime) {
		const std::string d = deluge::serialization::first_difference(
		    deluge::instrument::dump_state(sound.seededState()), deluge::instrument::dump_state(record));
		if (!d.empty()) {
			return "runtime at " + d;
		}
		if (const deluge::effects::EffectStates played = sound.seededState().effects; !(played == record.effects)) {
			const deluge::engine::CodecContext context{.input_tick_magnitude = 1};
			return "runtime at effects "
			       + deluge::serialization::first_difference(deluge::effects::encode(played, context),
			                                                 deluge::effects::encode(record.effects, context));
		}
	}
	if (checks.slot) {
		const deluge::project::InstrumentSlot* slot = song.project.resolveSlot(*address);
		if (slot == nullptr) {
			return "slot missing";
		}
		const Value encoded = deluge::instrument::encode(record, {.input_tick_magnitude = slot->tick_magnitude});
		if (std::string d = stateDifference(slot->state, encoded); !d.empty()) {
			return "slot at " + d;
		}
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
		const deluge::project::InstrumentSlot* slot = song.project.resolveSlot(*address);
		if (slot == nullptr) {
			return "decode: slot missing";
		}
		const auto decoded =
		    deluge::instrument::decode_onto(slot->state, record, {.input_tick_magnitude = slot->tick_magnitude});
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
		const deluge::snapshot::SoundSnapshot before = sound.snapshot();
		sound.projectSnapshot();
		if (!(sound.snapshot() == before)) {
			return "snapshot";
		}
	}
	return {};
}

namespace {

/// Where @p record and @p runtime first differ, or empty.
std::string chainDifference(const deluge::project::FxChain& record, const deluge::project::FxChain& runtime) {
	if (record.slots.size() != runtime.slots.size()) {
		return "slot count " + std::to_string(record.slots.size()) + " vs " + std::to_string(runtime.slots.size());
	}
	for (size_t s = 0; s < record.slots.size(); s++) {
		const deluge::project::FxSlot& a = record.slots[s];
		const deluge::project::FxSlot& b = runtime.slots[s];
		if (a.effect != b.effect) {
			return "slot " + std::to_string(s) + " is " + a.effect + " vs " + b.effect;
		}
		if (const std::string d = deluge::serialization::first_difference(a.state, b.state); !d.empty()) {
			return a.effect + " " + d;
		}
		if (a.tick_magnitude != b.tick_magnitude) {
			return a.effect + " tick magnitude";
		}
	}
	if (const std::string d = deluge::serialization::first_difference(record.view, runtime.view); !d.empty()) {
		return "view " + d;
	}
	return record.reverb_return == runtime.reverb_return ? std::string{} : "reverb return";
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
			::GlobalEffectable* owner = deluge::bridge::chainOwnerAt(
			    {.kind = deluge::project::ChainAddress::Kind::OUTPUT, .id = *output->id()}, song);
			const int32_t magnitude = record->effects->slots.empty() ? song.getInputTickMagnitude()
			                                                         : record->effects->slots[0].tick_magnitude;
			if (const std::string d = chainDifference(
			        *record->effects,
			        deluge::bridge::encodeChainOf(owner->chainState(), deluge::project::ChainHome::CLIP, magnitude));
			    !d.empty()) {
				return where + d;
			}
			if (owner->modControllableSnapshot().effects != owner->chainState().effects) {
				return where + "render copy";
			}
			if (owner->modControllableSnapshot().mod_fx_param != owner->chainState().mod_fx_param) {
				return where + "render copy's mod FX knob";
			}
		}
	}
	const deluge::project::FxChain& master = song.project.master_effects;
	const int32_t magnitude = master.slots.empty() ? song.getInputTickMagnitude() : master.slots[0].tick_magnitude;
	if (const std::string d =
	        chainDifference(master, deluge::bridge::encodeChainOf(song.live.global_effectable.chainState(),
	                                                              deluge::project::ChainHome::MASTER, magnitude));
	    !d.empty()) {
		return "master effects: " + d;
	}
	if (song.live.global_effectable.modControllableSnapshot().effects
	    != song.live.global_effectable.chainState().effects) {
		return "master effects: render copy";
	}
	if (song.live.global_effectable.modControllableSnapshot().mod_fx_param
	    != song.live.global_effectable.chainState().mod_fx_param) {
		return "master effects: render copy's mod FX knob";
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
