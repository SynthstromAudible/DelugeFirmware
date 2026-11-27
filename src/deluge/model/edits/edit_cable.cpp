/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "model/edits/edit_cable.h"

#include "memory/heaps.h" // alloc_sdram
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/clip.h"
#include "model/consequence/consequence_instrument_delta.h"
#include "model/edits/param_address_for.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_selection.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"

#include <new>
#include <optional>
#include <utility>

namespace deluge::edits {

void recordCableEdit(ModelStackWithThreeMainThings const* modelStack, CableEdit edit) {
	// The edit that caused it is the open Action, whatever its type; with none open, a parameter change's.
	Action* action = actionLogger.firstAction[BEFORE];
	if (action == nullptr || !action->openForAdditions) {
		action = actionLogger.getNewAction(actionLogger.getParameterChangeActionType(), ActionAddition::ALLOWED);
	}
	if (action == nullptr) {
		return;
	}
	const std::optional<project::ParamAddress> owner = ownerAddressFor(*modelStack);
	if (!owner) {
		return;
	}
	if (void* memory = deluge::memory::alloc_sdram(sizeof(ConsequenceInstrumentDelta)); memory != nullptr) {
		action->addConsequence(new (memory) ConsequenceInstrumentDelta(*owner, std::move(edit)));
	}
}

ModelStackWithAutoParam* cableDepthForEdit(ModelStackWithThreeMainThings* modelStack, PatchSource source,
                                           deluge::synth::CableTarget target) {
	const int32_t paramId = PatchCableSet::getParamId(target, source);
	ModelStackWithAutoParam* depth = modelStack->getPatchCableAutoParamFromId(paramId, true);
	return depth != nullptr && depth->autoParam != nullptr ? depth : nullptr;
}

void settleCableDepth(const bridge::LaneTarget& lane) {
	Sound* const* sound = std::get_if<Sound*>(&lane.owner);
	if (sound == nullptr || lane.auto_param == nullptr || lane.collection == nullptr
	    || !bridge::isCableCollection(lane.collection)) {
		return;
	}
	PatchSource source;
	deluge::synth::CableTarget target;
	PatchCableSet::dissectParamId(lane.param_id, &target, &source);
	const auto& cables = *static_cast<PatchCableSet*>(lane.collection);
	const std::optional<size_t> index = cables.records().find(source, target);
	if (!index) {
		return;
	}
	const bool clipActive = lane.timeline == nullptr || static_cast<Clip*>(lane.timeline)->isActiveOnOutput();
	ModMatrixHost matrix{**sound, *lane.manager};
	if (std::optional<CableEdit> removed = matrix.settleDepth(*index, clipActive)) {
		// The undo record is made from an engine stack.
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		recordCableEdit(bridge::owner_stack_of(lane, memory), std::move(*removed));
	}
}

EditCable EditCable::add(ModelStackWithThreeMainThings* modelStack, deluge::synth::CableRecord cable) {
	return {modelStack, deluge::instruments::classic::CableDelta::Op::ADD, cable, 0};
}

EditCable EditCable::remove(ModelStackWithThreeMainThings* modelStack, size_t index) {
	return {modelStack, deluge::instruments::classic::CableDelta::Op::REMOVE, {}, index};
}

EditCable EditCable::setPolarity(ModelStackWithThreeMainThings* modelStack, size_t index,
                                 deluge::synth::CablePolarity polarity) {
	return {modelStack, deluge::instruments::classic::CableDelta::Op::SET_POLARITY, {.polarity = polarity}, index};
}

EditCable EditCable::reroute(ModelStackWithThreeMainThings* modelStack, size_t index,
                             deluge::synth::CableTarget target) {
	return {modelStack, deluge::instruments::classic::CableDelta::Op::REROUTE, {.target = target}, index};
}

Error EditCable::apply(Context& /*ctx*/) {
	ModMatrixHost matrix{deluge::model::sound_of(modelStack_->owner), *modelStack_->paramManager};
	std::optional<CableEdit> edit;
	switch (op_) {
	case deluge::instruments::classic::CableDelta::Op::ADD:
		edit = matrix.addCable(cable_);
		break;
	case deluge::instruments::classic::CableDelta::Op::REMOVE:
		edit = matrix.removeCable(index_);
		break;
	case deluge::instruments::classic::CableDelta::Op::SET_POLARITY:
		edit = matrix.setPolarity(index_, cable_.polarity);
		break;
	case deluge::instruments::classic::CableDelta::Op::REROUTE:
		edit = matrix.reroute(index_, cable_.target);
		break;
	}
	if (!edit) {
		return Error::UNSPECIFIED;
	}
	recordCableEdit(modelStack_, std::move(*edit));
	return Error::NONE;
}

} // namespace deluge::edits
