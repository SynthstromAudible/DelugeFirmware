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
#include "model/edits/edit.h"
#include "model/edits/set_dx_patch.h"
#include "model/edits/set_source_ranges.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/setting_apply.h"
#include "model/project_bridge/source_bridge.h"

#include "deluge-project/structure_codec.h"
#include "instruments/deluge/codec.h"
#include "instruments/deluge/deluge_synth.h"
#include "instruments/deluge/settings.h"
#include "instruments/deluge/state_access.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"
#include "plugin-api/types/plugin_id.h"
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

void adoptSoundState(::Sound& sound, std::optional<deluge::project::InstrumentSlot>& slot, const ::Song& song) {
	if (slot && slot->state.as<deluge::instrument::DelugeSynth>() != nullptr
	    && slot->state.as<deluge::instrument::DelugeSynth>() == sound.boundState()) {
		return; // the sound plays this slot's state already
	}
	// What the sound plays moves into the record, which it then plays: its own state, or a copy of the record it
	// plays now, which stays that record's.
	slot =
	    deluge::project::InstrumentSlot{.plugin = std::string(deluge::plugin::kDelugeInstrument.name),
	                                    .state = deluge::plugin::StateHandle::of<deluge::instrument::DelugeSynth>({}),
	                                    .tick_magnitude = song.getInputTickMagnitude()};
	sound.bindState(*slot->state.as<deluge::instrument::DelugeSynth>(), slot->routing);
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

Error commitState(::Sound& sound, const deluge::instrument::SlotChange& change, ::Song& song) {
	deluge::instrument::SoundState* record = sound.boundRecord();
	if (record == nullptr) {
		return Error::UNSPECIFIED;
	}
	const deluge::instrument::SoundState before = *record;
	*record = change.state;
	Error error = applyChange(sound, before, change.state, song);
	// The runtime reshapes ranges as it takes a type (keeping only top notes across kinds), which it reflects into the
	// record: the gates' audit checks that the record is the change's afterwards.
	if (error == Error::NONE && typedWriteAudit().enabled && !(*record == change.state)) {
		error = Error::UNSPECIFIED;
	}
	return error;
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
                     const std::expected<deluge::instrument::SlotChange, deluge::plugin::StateError>& got,
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

void auditSettingWrite(const deluge::instrument::SoundState& before, std::string_view path,
                       const deluge::serialization::Value& value,
                       const std::expected<deluge::instrument::SlotChange, deluge::plugin::StateError>& want,
                       const std::expected<deluge::instrument::SlotChange, deluge::plugin::StateError>& got) {
	TypedWriteAudit& audit = typedWriteAudit();
	if (!audit.enabled) {
		return;
	}
	audit.writes++;
	std::string_view why;
	if (want.has_value() != got.has_value()) {
		why = " (one refused)";
	}
	else if (want && got->state == want->state) {
		const bool named = std::ranges::all_of(
		    got->changed, [&](std::string_view key) { return std::ranges::contains(want->changed, key); });
		if (got->changed != deluge::instrument::changed_entries(before, got->state) || !named) {
			why = " (touched entries)";
		}
	}
	else if (want) {
		// The old way lost a write the file cannot hold: the typed state is the old way's with that field written.
		deluge::instrument::SoundState again = want->state;
		(void)deluge::instrument::write_setting(again, path, value);
		const std::string_view key = path.substr(0, path.find('/'));
		if (!(again == got->state)
		    || deluge::instrument::changed_entries(want->state, got->state) != std::vector<std::string_view>{key}) {
			why = " (state)";
		}
	}
	if (!why.empty()) {
		audit.mismatches++;
		if (audit.first.empty()) {
			audit.first = std::string(path) + "=" + value.str() + std::string(why);
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
		AudioExcludedSection section; // the render reads the effects' projection
		sound.projectEffects();
	}
	// What loading made of each oscillator reaches its record as the Edits a gesture would make, in this tick: the
	// ranges' clamped zones and found files, and a DX7 patch the runtime made for one decoded without its own
	// (a source whose record already holds what it loaded takes no Edit)
	deluge::edits::Context ctx{song, song.project};
	for (int32_t s = 0; s < kNumSources; s++) {
		const auto id = static_cast<deluge::instrument::SourceId>(s);
		const deluge::instrument::DxPatchState* recordedPatch = deluge::instrument::dx_patch(*sound.boundState(), id);
		if (std::optional<deluge::instrument::DxPatchState> patch = dxPatchOf(sound.sourceResources[s]);
		    patch && (recordedPatch == nullptr || *patch != *recordedPatch)) {
			deluge::edits::SetDxPatch dx{*address, static_cast<uint8_t>(s), std::move(*patch)};
			(void)deluge::edits::apply(ctx, dx);
		}
		if (std::vector<deluge::instrument::RangeState> held = rangesOf(sound.sourceResources[s]);
		    !std::ranges::equal(held, deluge::instrument::source_ranges(*sound.boundState(), id))) {
			deluge::edits::SetSourceRanges ranges{*address, static_cast<uint8_t>(s), std::move(held)};
			(void)deluge::edits::apply(ctx, ranges);
		}
	}
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
