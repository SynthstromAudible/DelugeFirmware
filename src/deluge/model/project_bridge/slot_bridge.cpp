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

#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/source_bridge.h"

#include "deluge-project/structure_codec.h"
#include "engine-api/types/engine_id.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/engine.h"
#include "engines/deluge/settings.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/sound_instrument.h"
#include "serialization/tree.h"
#include "storage/storage_manager.h"
#include "storage/value_serializer.h"

#include <algorithm>
#include <optional>
#include <string>

namespace deluge::bridge {

void fillSlot(::Sound& sound, deluge::project::InstrumentSlot& slot, bool /*isDrum*/, const ::Song& song) {
	slot = deluge::project::InstrumentSlot{.engine = std::string(deluge::engine::kDelugeInstrument.name),
	                                       .tick_magnitude = song.getInputTickMagnitude()};
	syncSlotState(sound, slot, song);
}

namespace {
deluge::project::InstrumentSlot* slotOf(::Sound& sound);
} // namespace

void refreshSlots(::Song& song) {
	// A sound's slot holds its state: after a save, each takes what its sound holds, as every commit leaves it.
	const auto sync = [&](::Sound& sound) {
		if (deluge::project::InstrumentSlot* slot = slotOf(sound)) {
			syncSlotState(sound, *slot, song);
		}
	};
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->type == OutputType::SYNTH) {
			sync(*static_cast<SoundInstrument*>(output));
		}
		else if (output->type == OutputType::KIT) {
			for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
				if (drum->type == DrumType::SOUND) {
					sync(*static_cast<SoundDrum*>(drum));
				}
			}
		}
	}
}

namespace {

/// The record slot of @p sound, when it is bound.
deluge::project::InstrumentSlot* slotOf(::Sound& sound) {
	if (sound.boundState() == nullptr) {
		return nullptr;
	}
	std::optional<deluge::project::InstrumentSlot>* slot = nullptr;
	if (sound.isDrum()) {
		auto& drum = static_cast<SoundDrum&>(sound);
		slot = drum.docDrum_ != nullptr ? &drum.docDrum_->instrument : nullptr;
	}
	else {
		auto& synth = static_cast<SoundInstrument&>(sound);
		slot = synth.docOutput_ != nullptr ? &synth.docOutput_->instrument : nullptr;
	}
	return slot != nullptr && slot->has_value() ? &**slot : nullptr;
}

/// The sound of @p output whose slot @p address names, or nullptr.
::Sound* soundIn(Output& output, deluge::project::SlotAddress address) {
	using Kind = deluge::project::SlotAddress::Kind;
	if (output.type == OutputType::SYNTH && address.kind == Kind::OUTPUT && output.id() == address.id) {
		return static_cast<SoundInstrument*>(&output);
	}
	if (output.type == OutputType::KIT && address.kind == Kind::DRUM) {
		for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND && drum->id() == address.id) {
				return static_cast<SoundDrum*>(drum);
			}
		}
	}
	return nullptr;
}

} // namespace

std::optional<deluge::project::SlotAddress> slotAddressOf(::Sound& sound) {
	if (slotOf(sound) == nullptr) {
		return std::nullopt;
	}
	using Kind = deluge::project::SlotAddress::Kind;
	if (sound.isDrum()) {
		return deluge::project::SlotAddress{Kind::DRUM, *static_cast<SoundDrum&>(sound).id()};
	}
	return deluge::project::SlotAddress{Kind::OUTPUT, *static_cast<SoundInstrument&>(sound).id()};
}

::Sound* soundAt(::Song& song, deluge::project::SlotAddress address) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (::Sound* sound = soundIn(*output, address)) {
			return sound;
		}
	}
	for (Instrument* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = static_cast<Instrument*>(sleeping->next)) {
		if (::Sound* sound = soundIn(*sleeping, address)) {
			return sound;
		}
	}
	return nullptr;
}

::Kit* kitAt(::Song& song, uint32_t id) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->id() == id && output->type == OutputType::KIT) {
			return static_cast<Kit*>(output);
		}
	}
	return nullptr;
}

void syncSlotState(const ::Sound& sound, deluge::project::InstrumentSlot& slot, const ::Song& /*song*/) {
	(void)refreshSlot(sound, slot);
}

deluge::serialization::Value withLeaf(const deluge::serialization::Value& tree, std::string_view path,
                                      deluge::serialization::Value leaf) {
	const size_t slash = path.find('/');
	const std::string_view key = path.substr(0, slash);
	const deluge::serialization::Value* held = tree.isMap() ? tree.find(key) : nullptr;
	deluge::serialization::Value value =
	    slash == std::string_view::npos ? std::move(leaf)
	                                    : withLeaf(held != nullptr ? *held : deluge::serialization::Value::makeMap(),
	                                               path.substr(slash + 1), std::move(leaf));
	deluge::serialization::Value out = deluge::serialization::Value::makeMap();
	bool placed = false;
	if (tree.isMap()) {
		for (const auto& [name, entry] : tree.entries()) {
			if (name == key) {
				out.set(name, std::move(value));
				placed = true;
			}
			else {
				out.set(name, entry);
			}
		}
	}
	if (!placed) {
		out.set(std::string(key), std::move(value));
	}
	return out;
}

Error applyDifference(::Sound& sound, const deluge::instrument::SoundState& old,
                      const deluge::instrument::SoundState& next, std::span<const std::string_view> changed,
                      ::Song& song) {
	for (const deluge::engine::SettingInfo& row : deluge::instrument::kSettings) {
		const std::string_view entry = row.path.substr(0, row.path.find('/'));
		if (row.kind == deluge::engine::SettingKind::STRUCTURED || std::ranges::find(changed, entry) == changed.end()) {
			continue; // an entry the change did not touch holds what the sound holds
		}
		const std::optional<deluge::serialization::Value> before = deluge::instrument::setting_value(old, row.path);
		const std::optional<deluge::serialization::Value> after = deluge::instrument::setting_value(next, row.path);
		if (after && (!before || before->str() != after->str())) {
			if (Error error = applySetting(sound, row.path, *after, song); error != Error::NONE) {
				return error;
			}
		}
	}
	for (int32_t s = 0; s < kNumSources; s++) {
		// After the settings, an oscillator type among them.
		const deluge::instrument::SourceState& now = sound.boundState()->sources[s];
		const deluge::instrument::SourceState& wanted = next.sources[s];
		if (wanted.dx_patch && now.dx_patch != wanted.dx_patch && sound.sources[s].oscType == OscType::DX7) {
			if (Error error = applyDxPatch(sound, s, *wanted.dx_patch); error != Error::NONE) {
				return error;
			}
		}
		if (now.ranges != wanted.ranges) {
			if (Error error = applyRanges(sound, s, wanted.ranges); error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}

Error commitState(::Sound& sound, const deluge::instrument::SlotChange& change, ::Song& song) {
	if (sound.boundState() == nullptr) {
		return Error::UNSPECIFIED;
	}
	const deluge::instrument::SoundState before = *sound.boundState();
	Error error = applyDifference(sound, before, change.state, change.changed, song);
	if (error == Error::NONE && !(*sound.boundState() == change.state)) {
		error = Error::UNSPECIFIED; // the runtime did not reach the state the change reads as
	}
	return error;
}

deluge::serialization::Value refreshSlot(const ::Sound& sound, deluge::project::InstrumentSlot& slot) {
	if (sound.boundState() != nullptr) {
		if (deluge::instrument::SoundState* held = slot.state.as<deluge::instrument::DelugeInstrument>()) {
			*held = *sound.boundState();
		}
		else {
			slot.state = deluge::engine::StateHandle::of<deluge::instrument::DelugeInstrument>(*sound.boundState());
		}
	}
	// At the magnitude the slot was filled at, as its edits read and write it.
	return slot.state.encode({.input_tick_magnitude = slot.tick_magnitude});
}

std::optional<deluge::serialization::Value> refreshedSlotState(::Song& song, deluge::project::SlotAddress address) {
	deluge::project::InstrumentSlot* slot = song.project.resolveSlot(address);
	::Sound* sound = soundAt(song, address);
	return slot != nullptr && sound != nullptr ? std::optional{refreshSlot(*sound, *slot)} : std::nullopt;
}

TypedWriteAudit& typedWriteAudit() {
	static TypedWriteAudit audit;
	return audit;
}

std::optional<deluge::serialization::Value> typedWriteAuditBase(const ::Sound& sound,
                                                                const deluge::project::InstrumentSlot& slot) {
	if (!typedWriteAudit().enabled || sound.boundState() == nullptr) {
		return std::nullopt;
	}
	return deluge::instrument::encode(*sound.boundState(), {.input_tick_magnitude = slot.tick_magnitude});
}

void auditTypedWrite(const ::Sound& sound, const deluge::project::InstrumentSlot& slot,
                     const deluge::serialization::Value& whole, const deluge::serialization::Value& element,
                     const std::expected<deluge::instrument::SlotChange, deluge::engine::StateError>& got,
                     std::string_view what) {
	TypedWriteAudit& audit = typedWriteAudit();
	const auto want = deluge::instrument::decode_change(element, whole, *sound.boundState(),
	                                                    {.input_tick_magnitude = slot.tick_magnitude});
	audit.writes++;
	const bool same =
	    want.has_value() == got.has_value() && (!want || (want->state == got->state && want->changed == got->changed));
	if (!same) {
		audit.mismatches++;
		if (audit.first.empty()) {
			audit.first = std::string(what)
			              + (want.has_value() != got.has_value()
			                     ? " (one refused)"
			                     : (want->changed != got->changed ? " (touched entries)" : " (state)"));
		}
	}
}

void recordLoadedSound(::Sound& sound, ::Song& song) {
	const std::optional<deluge::project::SlotAddress> address = slotAddressOf(sound);
	deluge::project::InstrumentSlot* slot = address ? song.project.resolveSlot(*address) : nullptr;
	if (slot == nullptr) {
		return;
	}
	{
		AudioExcludedSection section; // the reseed reprojects the snapshot the render reads
		sound.reseedRecord();
	}
	syncSlotState(sound, *slot, song);
	if (const std::optional<deluge::project::ControlsAddress> owner = controlsAddressOf(sound)) {
		if (std::vector<deluge::project::ControllerMapping>* controls = song.project.resolveControls(*owner)) {
			*controls = controlsOf(sound); // the knobs the load read
		}
	}
}

void recordLoadedOutput(::Output& output, ::Song& song) {
	auto record = [&](::Sound& sound) { recordLoadedSound(sound, song); };
	if (output.type == OutputType::SYNTH) {
		record(*static_cast<SoundInstrument*>(&output));
	}
	else if (output.type == OutputType::KIT) {
		for (Drum* drum = static_cast<Kit&>(output).firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type == DrumType::SOUND) {
				record(*static_cast<SoundDrum*>(drum));
			}
		}
	}
}

void recordLoadedSounds(::Song& song) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		recordLoadedOutput(*output, song);
	}
	for (Output* sleeping = song.live.media.first_hibernating_instrument; sleeping != nullptr;
	     sleeping = sleeping->next) {
		recordLoadedOutput(*sleeping, song);
	}
}

} // namespace deluge::bridge
