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
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/consequence/consequence.h"
#include "model/output_parameter_visitors.h"
#include "modulation/automation/auto_param.h"

namespace {
using deluge::modulation::params::Kind;
class PresetParamChange final : public Consequence {
public:
	Output* output;
	ModControllable* owner;
	Kind kind;
	int32_t id;
	int32_t value;
	PresetParamChange(Output* output, ModControllable* owner, Kind kind, int32_t id, int32_t value)
	    : output(output), owner(owner), kind(kind), id(id), value(value) {
		type = PRESET_PARAM_CHANGE;
	}
	Error revert(TimeType /*time*/, ModelStack* stack) override {
		auto* preset = owner->getPresetValueStorage(kind, id);
		if (preset == nullptr && kind != Kind::PATCH_CABLE) {
			return Error::INSUFFICIENT_RAM;
		}
		int32_t previous = preset != nullptr ? *preset : 0;
		Error error = output->setPresetParameter(stack->song, owner, kind, id, value, false);
		if (error == Error::NONE) {
			value = previous;
		}
		return error;
	}
};
void recordPresetChange(Output* output, ModControllable* owner, Kind kind, int32_t id, int32_t previous) {
	Action* action = actionLogger.getParameterChangeAction(ActionOrigin::LINKED_PARAMETER_MENU);
	if (action != nullptr) {
		bool found = false;
		for (auto* cons = action->firstConsequence; cons != nullptr; cons = cons->next) {
			if (cons->type != Consequence::PRESET_PARAM_CHANGE) {
				continue;
			}
			auto* change = static_cast<PresetParamChange*>(cons);
			found |= change->output == output && change->owner == owner && change->kind == kind && change->id == id;
		}
		if (!found) {
			void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(PresetParamChange));
			if (memory != nullptr) {
				// Ownership passes to the action, which destroys and deallocates the consequence.
				// NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
				auto* change = new (memory) PresetParamChange(output, owner, kind, id, previous);
				action->addConsequence(change);
			}
		}
	}
}
} // namespace

Error Output::setPresetParameter(Song* song, ModControllable* owner, Kind kind, int32_t id, int32_t value,
                                 bool recordUndo) {
	if (owner == nullptr) {
		return Error::NONE;
	}
	auto* preset = owner->getPresetValueStorage(kind, id);
	if (preset == nullptr && kind == Kind::PATCH_CABLE) {
		preset = static_cast<ModControllableAudio*>(owner)->reuseZeroPresetPatchCable(song, id);
	}
	if (preset == nullptr) {
		return Error::INSUFFICIENT_RAM;
	}
	int32_t previous = *preset;
	if (previous == value) {
		return Error::NONE;
	}

	// Visit both session and arrangement clips, including the matching drum row
	// when the edited baseline belongs to a sound within a kit.

	// Materialize missing dynamic parameters before changing the shared value.
	// New entries inherit the old baseline with no local adjustment.
	Error error = visitOutputParameters(this, song, owner, kind, id, [&](ModelStackWithParamId* stack, Clip*) -> Error {
		owner->bindPresetValues(stack->paramManager);
		auto* collection = stack->paramCollection;
		bool existed = collection->getClipValueStorage(id) != nullptr;
		auto* param = collection->getAutoParamFromId(stack, true)->autoParam;
		if (!param) {
			return Error::INSUFFICIENT_RAM;
		}
		if (!existed) {
			collection->getClipValueStorage(id)->setValue(previous);
		}
		return Error::NONE;
	});
	if (error != Error::NONE) {
		return error;
	}

	if (recordUndo) {
		recordPresetChange(this, owner, kind, id, previous);
	}
	*preset = value;
	return visitOutputParameters(this, song, owner, kind, id, [&](ModelStackWithParamId* stack, Clip* clip) -> Error {
		auto* collection = stack->paramCollection;
		auto* scalar = collection->getClipValueStorage(id);
		auto* param_stack = collection->getAutoParamFromId(stack, false);
		if (!scalar || !param_stack->autoParam) {
			return Error::NONE;
		}
		auto* param = param_stack->autoParam;
		int32_t old_scalar = ClipParamValue::resolve(int64_t{previous} + scalar->getOffset());
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
