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

#include "deluge-project/instrument_slot.h"
#include "engines/deluge/codec.h"
#include "model/drum/drum.h"
#include "model/instrument/instrument.h"
#include "model/instrument/kit.h"
#include "model/output.h"
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
/// values differ. Empty when they agree. Order is not compared: a slot keeps a file's order, and appends what the file
/// lacked.
std::string stateDifference(const Value& slot, const Value& encoded) {
	for (const auto& [key, value] : encoded.entries()) {
		const Value* held = slot.find(key);
		if (held == nullptr) {
			return key + " (missing)";
		}
		if (std::string d = deluge::serialization::first_difference(*held, value, key); !d.empty()) {
			return d;
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
	}
	if (checks.slot) {
		const deluge::project::InstrumentSlot* slot = song.project.resolveSlot(*address);
		if (slot == nullptr) {
			return "slot missing";
		}
		const Value encoded =
		    deluge::instrument::encode(record, {.input_tick_magnitude = song.getInputTickMagnitude()});
		if (std::string d = stateDifference(slot->state, encoded); !d.empty()) {
			return "slot at " + d;
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

int32_t expectSongCoherent(::Song& song, const char* leg, CoherenceChecks checks) {
	int32_t failures = 0;
	auto check = [&](Sound& sound, const std::string& owner) {
		if (const std::string what = soundIncoherence(sound, song, checks); !what.empty()) {
			std::printf("SLOT-COHERENT FAIL leg=%s %s: %s\n", leg, owner.c_str(), what.c_str());
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
	return failures;
}

} // namespace deluge::harness
