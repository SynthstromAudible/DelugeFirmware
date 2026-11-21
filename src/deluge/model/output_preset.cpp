/*
 * Copyright © 2026 Sean Ditny
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

#include "model/instrument/kit.h"
#include "model/output_parameter_visitors.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/preset_save_values.h"

using deluge::modulation::params::Kind;

Error Output::rebasePresetParameter(Song* song, ModControllable* owner, Kind kind, int32_t id, int32_t value) {
	auto* preset = owner->getPresetValueStorage(kind, id);
	if (preset == nullptr && kind == Kind::PATCH_CABLE) {
		preset = static_cast<ModControllableAudio*>(owner)->reuseZeroPresetPatchCable(song, id);
	}
	if (preset == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	if (*preset == value) {
		return Error::NONE;
	}
	Error error = visitOutputParameters(this, song, owner, kind, id, [&](ModelStackWithParamId* stack, Clip*) {
		owner->bindPresetValues(stack->paramManager);
		bool existed = stack->paramCollection->getClipValueStorage(id) != nullptr;
		auto* param = stack->paramCollection->getAutoParamFromId(stack, true)->autoParam;
		if (param == nullptr) {
			return Error::INSUFFICIENT_RAM;
		}
		if (!existed) {
			stack->paramCollection->getClipValueStorage(id)->setValue(*preset);
		}
		return Error::NONE;
	});
	if (error != Error::NONE) {
		return error;
	}
	auto preserve = [&](ParamManager& manager) {
		for (auto* summary = manager.summaries; summary->paramCollection != nullptr; ++summary) {
			auto* collection = summary->paramCollection;
			if (collection->getParamKind() == kind) {
				auto* scalar = collection->getClipValueStorage(id);
				if (scalar != nullptr) {
					scalar->preserveBeforePresetChange(preset, value);
				}
			}
		}
	};
	for (Clip* candidate : AllClips::everywhere(song)) {
		preserve(candidate->paramManager);
		if (candidate->type == ClipType::INSTRUMENT) {
			auto* instrument_clip = static_cast<InstrumentClip*>(candidate);
			preserve(instrument_clip->backedUpParamManagerMIDI);
			auto& rows = instrument_clip->noteRows;
			for (int32_t r = 0; r < rows.getNumElements(); ++r) {
				preserve(rows.getElement(r)->paramManager);
			}
		}
	}
	for (int32_t i = 0; i < song->backedUpParamManagers.getNumElements(); ++i) {
		auto* backup = static_cast<BackedUpParamManager*>(song->backedUpParamManagers.getElementAddress(i));
		preserve(backup->paramManager);
	}
	*preset = value;
	return Error::NONE;
}

namespace {
Error captureSavedManager(Output* output, Song* song, ModControllable* owner, ParamManager* manager,
                          PresetSaveValues& saved) {
	owner->bindPresetValues(manager);
	for (auto* summary = manager->summaries; summary->paramCollection != nullptr; ++summary) {
		auto* collection = summary->paramCollection;
		Kind kind = collection->getParamKind();
		if (kind == Kind::EXPRESSION) {
			continue;
		}
		int32_t count = 0;
		if (kind == Kind::PATCH_CABLE) {
			count = static_cast<PatchCableSet*>(collection)->numPatchCables;
		}
		else if (kind == Kind::MIDI) {
			count = static_cast<MIDIParamCollection*>(collection)->params.getNumElements();
		}
		else {
			count = static_cast<ParamSet*>(collection)->getNumParams();
		}
		for (int32_t i = 0; i < count; ++i) {
			int32_t id = i;
			AutoParam* param = nullptr;
			if (kind == Kind::PATCH_CABLE) {
				auto& cable = static_cast<PatchCableSet*>(collection)->patchCables[i];
				id = PatchCableSet::getParamId(cable.destinationParamDescriptor, cable.from);
				param = &cable.param;
			}
			else if (kind == Kind::MIDI) {
				auto* entry = static_cast<MIDIParamCollection*>(collection)->params.getElement(i);
				id = static_cast<int32_t>(entry->cc);
				param = &entry->param;
			}
			else {
				param = &static_cast<ParamSet*>(collection)->params[i];
			}
			auto value = saved.find(&param->data());
			if (!value.has_value()) {
				continue;
			}
			Error error = output->rebasePresetParameter(song, owner, kind, id, *value);
			if (error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}
} // namespace

Error Output::captureSavedPreset(Song* song, Clip* clip, PresetSaveValues& saved) {
	if (type == OutputType::KIT) {
		Error global_error = captureSavedManager(this, song, toModControllable(), &clip->paramManager, saved);
		if (global_error != Error::NONE) {
			return global_error;
		}
		auto* kit = static_cast<Kit*>(this);
		for (auto* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->type != DrumType::SOUND) {
				continue;
			}
			auto* row = static_cast<InstrumentClip*>(clip)->getNoteRowForDrum(drum);
			if (row == nullptr) {
				row = song->findNoteRowForDrum(kit, drum);
			}
			if (row != nullptr) {
				Error error = captureSavedManager(this, song, static_cast<SoundDrum*>(drum), &row->paramManager, saved);
				if (error != Error::NONE) {
					return error;
				}
			}
		}
		return Error::NONE;
	}
	return captureSavedManager(this, song, toModControllable(), &clip->paramManager, saved);
}
