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

#include "model/project_bridge/lane_target.h"

#include "model/model_stack.h"
#include "modulation/params/param_collection.h"

namespace deluge::bridge {

::ModelStackWithThreeMainThings* owner_stack_of(const LaneTarget& target, void* memory) {
	return setupModelStackWithSong(memory, target.song)
	    ->addTimelineCounter(target.timeline)
	    ->addNoteRow(target.note_row_id, target.note_row)
	    ->addOtherTwoThings(target.owner, target.manager);
}

::ModelStackWithAutoParam* stack_of(const LaneTarget& target, void* memory) {
	return owner_stack_of(target, memory)
	    ->addParam(target.collection, target.summary, target.param_id, target.auto_param);
}

LaneTarget target_of(const ::ModelStackWithAutoParam& stack, std::optional<project::ParamAddress> address) {
	return LaneTarget{
	    .address = address,
	    .song = stack.song,
	    .timeline = stack.getTimelineCounterAllowNull(),
	    .note_row_id = stack.noteRowId,
	    .note_row = stack.getNoteRowAllowNull(),
	    .owner = stack.owner,
	    .manager = stack.paramManager,
	    .collection = stack.paramCollection,
	    .summary = stack.summary,
	    .kind = stack.kind,
	    .param_id = stack.paramId,
	    .auto_param = stack.autoParam,
	};
}

project::DocAutoParam* doc_of(const LaneTarget& target) {
	alignas(::ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	return stack_of(target, memory)->doc();
}

void notify_modified(const ::ModelStackWithAutoParam& stack, int32_t oldValue, bool automationChanged,
                     bool automatedBefore, bool automatedNow) {
	stack.paramCollection->notifyParamModifiedInSomeWay(target_of(stack, std::nullopt), oldValue, automationChanged,
	                                                    automatedBefore, automatedNow);
}

} // namespace deluge::bridge
