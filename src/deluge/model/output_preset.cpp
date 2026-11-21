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

#include "gui/views/view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence_param_change.h"
#include "model/instrument/kit.h"
#include "model/output_parameter_visitors.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/patch/patch_cable_set.h"
#include "storage/preset_save_values.h"
#include <span>

using deluge::modulation::params::Kind;

namespace {
// Create the parameter in every clip and backed-up manager which lacks it, at the baseline with no offset,
// so it follows the baseline. This changes no value.
PLACE_SDRAM_TEXT Error materializePresetParameter(Output* output, Song* song, ModControllable* owner, Kind kind,
                                                  int32_t id, int32_t baseline) {
	return visitOutputParameters(
	    output, song, owner, kind, id,
	    [&](ModelStackWithParamId* stack, Clip*) {
		    owner->bindPresetValues(stack->paramManager);
		    bool existed = stack->paramCollection->getClipValueStorage(id) != nullptr;
		    auto* param = stack->paramCollection->getAutoParamFromId(stack, true)->autoParam;
		    if (param == nullptr) {
			    return Error::INSUFFICIENT_RAM;
		    }
		    if (!existed) {
			    stack->paramCollection->getClipValueStorage(id)->setValue(baseline);
		    }
		    return Error::NONE;
	    },
	    true);
}

// Undo records hold a clip's absolute value. Shift those of a clip which keeps its offset through a
// baseline change, so undoing its earlier edit restores that offset against the new baseline.
PLACE_SDRAM_TEXT void shiftClipHistory(const ParamCollection* collection, int32_t id, int64_t difference) {
	for (Action* list : actionLogger.firstAction) {
		for (Action* action = list; action != nullptr; action = action->nextAction) {
			for (auto* cons = action->firstConsequence; cons != nullptr; cons = cons->next) {
				if (cons->type != Consequence::PARAM_CHANGE) {
					continue;
				}
				auto* change = static_cast<ConsequenceParamChange*>(cons);
				// Only compare the recorded collection; never dereference it.
				// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
				const auto& recorded = change->modelStack;
				if (recorded.paramCollection != collection || recorded.paramId != id) {
					continue;
				}
				auto& state = change->state;
				int32_t old_base = ClipParamValue::resolve(state.clipValue);
				state.clipValue += difference;
				state.nodeBase = ClipParamValue::resolve(int64_t{state.nodeBase}
				                                         + ClipParamValue::resolve(state.clipValue) - old_base);
				state.value = ClipParamValue::resolve(int64_t{state.value} + difference);
			}
		}
	}
}
} // namespace

PLACE_SDRAM_TEXT Error Output::preparePresetParameter(Song* song, ModControllable* owner, Kind kind, int32_t id,
                                                      int32_t value) {
	if (kind == Kind::PATCH_CABLE) {
		// A route absent from the baseline table reads zero. The rebase finds it a slot. A zero
		// route follows its baseline by being absent, so clips lacking it need nothing.
		int32_t baseline = *static_cast<ModControllableAudio*>(owner)->getPresetPatchCableValues().find(id);
		return baseline == value || value == 0 ? Error::NONE
		                                       : materializePresetParameter(this, song, owner, kind, id, baseline);
	}
	auto* preset = owner->getPresetValueStorage(kind, id);
	if (preset == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	if (*preset == value) {
		return Error::NONE;
	}
	return materializePresetParameter(this, song, owner, kind, id, *preset);
}

PLACE_SDRAM_TEXT void Output::shiftPresetOffsets(Song* song, Kind kind, int32_t id, const int32_t* preset,
                                                 int64_t shift) {
	auto shift_manager = [&](ParamManager& manager) {
		for (auto* summary = manager.summaries; summary->paramCollection != nullptr; ++summary) {
			auto* collection = summary->paramCollection;
			if (collection->getParamKind() == kind) {
				auto* scalar = collection->getClipValueStorage(id);
				if (scalar != nullptr) {
					scalar->shiftOffset(preset, shift);
				}
			}
		}
	};
	for (Clip* candidate : AllClips::everywhere(song)) {
		shift_manager(candidate->paramManager);
		if (candidate->type == ClipType::INSTRUMENT) {
			auto* instrument_clip = static_cast<InstrumentClip*>(candidate);
			shift_manager(instrument_clip->backedUpParamManagerMIDI);
			auto& rows = instrument_clip->noteRows;
			for (int32_t r = 0; r < rows.getNumElements(); ++r) {
				shift_manager(rows.getElement(r)->paramManager);
			}
		}
	}
	for (int32_t i = 0; i < song->backedUpParamManagers.getNumElements(); ++i) {
		auto* backup = static_cast<BackedUpParamManager*>(song->backedUpParamManagers.getElementAddress(i));
		shift_manager(backup->paramManager);
	}
}

PLACE_SDRAM_TEXT Error Output::announcePresetParameterChange(Song* song, ModControllable* owner, Kind kind, int32_t id,
                                                             int32_t previous, const ParamManager* preserved,
                                                             int64_t offset_shift) {
	return visitOutputParameters(this, song, owner, kind, id, [&](ModelStackWithParamId* stack, Clip* clip) -> Error {
		auto* collection = stack->paramCollection;
		auto* scalar = collection->getClipValueStorage(id);
		auto* param_stack = collection->getAutoParamFromId(stack, false);
		if (!scalar || !param_stack->autoParam) {
			return Error::NONE;
		}
		auto* param = param_stack->autoParam;
		int32_t old_scalar = stack->paramManager == preserved
		                         ? scalar->getValue()
		                         : ClipParamValue::resolve(int64_t{previous} + scalar->getOffset() - offset_shift);
		int32_t old_value = ClipParamValue::resolve(int64_t{old_scalar} + param->getAutomationOffset());
		bool automated = param->isAutomated();
		// The displayed baseline changed even if an effective value was clamped,
		// or this is an inactive MIDI clip whose CC must not be transmitted.
		view.notifyParamAutomationOccurred(stack->paramManager);
		// Inactive MIDI clips must not transmit their different local values.
		if (kind != Kind::MIDI || clip->isActiveOnOutput()) {
			collection->notifyParamModifiedInSomeWay(param_stack, old_value, false, automated, automated);
		}
		return Error::NONE;
	});
}

Error Output::rebasePresetParameter(Song* song, ModControllable* owner, Kind kind, int32_t id, int32_t value,
                                    ParamManager* saved) {
	int32_t* preset = nullptr;
	if (kind == Kind::PATCH_CABLE) {
		auto* audio = static_cast<ModControllableAudio*>(owner);
		// Without a slot, a route already has the zero baseline.
		if (*audio->getPresetPatchCableValues().find(id) == value) {
			return Error::NONE;
		}
		preset = owner->getPresetValueStorage(kind, id);
		if (preset == nullptr) {
			preset = audio->reuseZeroPresetPatchCable(song, id);
		}
	}
	else {
		preset = owner->getPresetValueStorage(kind, id);
	}
	if (preset == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	int32_t previous = *preset;
	if (previous == value) {
		return Error::NONE;
	}
	if (kind != Kind::PATCH_CABLE || value != 0) {
		Error error = materializePresetParameter(this, song, owner, kind, id, previous);
		if (error != Error::NONE) {
			return error;
		}
	}
	// The saved manager keeps the values it saved. Every other clip, note row and backup keeps its
	// offset and relative curve, so moves with the baseline.
	for (auto* summary = saved->summaries; summary->paramCollection != nullptr; ++summary) {
		auto* collection = summary->paramCollection;
		auto* scalar = collection->getParamKind() == kind ? collection->getClipValueStorage(id) : nullptr;
		if (scalar != nullptr) {
			scalar->preserveBeforePresetChange(preset, value);
		}
	}
	rebasePresetHistory(owner, kind, id, int64_t{value} - previous);
	// Each other clip keeps its offset, so its undo records move with the baseline.
	Error shifted = visitOutputParameters(this, song, owner, kind, id, [&](ModelStackWithParamId* stack, Clip*) {
		if (stack->paramManager != saved) {
			shiftClipHistory(stack->paramCollection, id, int64_t{value} - previous);
		}
		return Error::NONE;
	});
	if (shifted != Error::NONE) {
		return shifted;
	}
	*preset = value;
	parameterBaselinesChanged();
	return announcePresetParameterChange(song, owner, kind, id, previous, saved);
}

namespace {
// Routes in the baseline table which the preset did not save get a zero baseline. Cleared before saved
// routes are rebased, so each saved route finds a slot: the table can then hold every route of one clip.
PLACE_SDRAM_TEXT Error clearUnsavedPatchCables(Output* output, Song* song, ModControllable* owner,
                                               ParamManager* manager, PresetSaveValues& saved) {
	const ParamCollection* cables = nullptr;
	for (auto* summary = manager->summaries; summary->paramCollection != nullptr; ++summary) {
		if (summary->paramCollection->getParamKind() == Kind::PATCH_CABLE) {
			cables = summary->paramCollection;
		}
	}
	if (cables == nullptr) {
		return Error::NONE;
	}
	const auto& table = static_cast<ModControllableAudio*>(owner)->getPresetPatchCableValues();
	// Clearing a route only zeroes its entry, so the table doesn't change shape.
	for (const auto& entry : std::span(table.entries).first(table.count)) {
		auto id = static_cast<int32_t>(entry.id);
		if (entry.value != 0 && !saved.find(cables, id).has_value()) {
			Error error = output->rebasePresetParameter(song, owner, Kind::PATCH_CABLE, id, 0, manager);
			if (error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}

// With apply false, only prepares storage, so a failure leaves every baseline unchanged.
PLACE_SDRAM_TEXT Error captureSavedManager(Output* output, Song* song, ModControllable* owner, ParamManager* manager,
                                           PresetSaveValues& saved, bool apply) {
	owner->bindPresetValues(manager);
	if (apply) {
		Error error = clearUnsavedPatchCables(output, song, owner, manager, saved);
		if (error != Error::NONE) {
			return error;
		}
	}
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
			if (kind == Kind::PATCH_CABLE) {
				auto& cable = static_cast<PatchCableSet*>(collection)->patchCables[i];
				id = PatchCableSet::getParamId(cable.destinationParamDescriptor, cable.from);
			}
			else if (kind == Kind::MIDI) {
				id = static_cast<int32_t>(static_cast<MIDIParamCollection*>(collection)->params.getElement(i)->cc);
			}
			auto value = saved.find(collection, id);
			if (!value.has_value()) {
				continue;
			}
			Error error = apply ? output->rebasePresetParameter(song, owner, kind, id, *value, manager)
			                    : output->preparePresetParameter(song, owner, kind, id, *value);
			if (error != Error::NONE) {
				return error;
			}
		}
	}
	return Error::NONE;
}
} // namespace

PLACE_SDRAM_TEXT Error Output::captureSavedPreset(Song* song, Clip* clip, PresetSaveValues& saved) {
	auto capture = [&](bool apply) {
		Error error = captureSavedManager(this, song, toModControllable(), &clip->paramManager, saved, apply);
		if (error != Error::NONE || type != OutputType::KIT) {
			return error;
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
				error =
				    captureSavedManager(this, song, static_cast<SoundDrum*>(drum), &row->paramManager, saved, apply);
				if (error != Error::NONE) {
					return error;
				}
			}
		}
		return Error::NONE;
	};
	// Prepare everything before rebasing anything.
	Error error = capture(false);
	return error != Error::NONE ? error : capture(true);
}

PLACE_SDRAM_TEXT Error Output::captureSavedDrumPreset(Song* song, SoundDrum* drum, ParamManager* manager,
                                                      PresetSaveValues& saved) {
	Error error = captureSavedManager(this, song, drum, manager, saved, false);
	return error != Error::NONE ? error : captureSavedManager(this, song, drum, manager, saved, true);
}
