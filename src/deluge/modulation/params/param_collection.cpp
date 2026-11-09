/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "modulation/params/param_collection.h"
#include "definitions_cxx.hpp"
#include "deluge-project/param_edit.h"
#include "libdeluge/system.h"
#include "memory/heaps.h"
#include "model/mod_controllable/delay_type.h"
#include "model/model_stack.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/working_copies.h"
#include "modulation/params/lane_publication.h"
#include "modulation/params/lane_snapshot.h"
#include "modulation/params/param_manager.h"
#include "playback/move_section.h"
#include "plugin-api/types/param_mapping.h"
#include <algorithm>
#include <vector>

namespace {
/// [render] or [interaction] The serial the next collection made is given.
std::atomic<uint32_t> g_nextSerial{1};
} // namespace

ParamCollection::ParamCollection(int32_t newObjectSize, ParamCollectionSummary* summary)
    : objectSize(newObjectSize), serial_(g_nextSerial.fetch_add(1, std::memory_order_relaxed)) {
	for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) { // Just do both even if we're only using one.
		summary->whichParamsAreAutomated[i] = 0;
	}

	for (int32_t i = 0; i < kMaxNumUnsignedIntegerstoRepAllParams; i++) { // Just do both even if we're only using one.
		summary->whichParamsAreInterpolating[i] = 0;
	}
	deluge::modulation::LanePublication::made(*this);
}

ParamCollection::ParamCollection(const ParamCollection& other)
    : objectSize(other.objectSize), ticksTilNextEvent(other.ticksTilNextEvent),
      serial_(g_nextSerial.fetch_add(1, std::memory_order_relaxed)) {
	deluge::modulation::LanePublication::made(*this);
}

deluge::project::LaneView ParamCollection::playedLane(int32_t paramId) {
	if (deluge::memory::onRenderTier() && hasWorkingCopies()) {
		if (const deluge::project::DocAutoParam* copy = deluge::modulation::workingCopy(*this, paramId)) {
			return deluge::project::LaneView::of(*copy); // a lane being recorded plays what the render writes
		}
	}
	if (const deluge::modulation::LaneSnapshot* snapshot = deluge::modulation::snapshotToPlay(*this)) {
		const deluge::modulation::PublishedLane* lane = snapshot->find(paramId);
		return (lane != nullptr) ? lane->view() : deluge::project::LaneView{};
	}
	deluge::modulation::noteRenderModelRead(); // in record mode, or a collection not yet published
	const deluge::project::DocAutoParam* lane = findDocParamForId(paramId);
	return (lane != nullptr) ? deluge::project::LaneView::of(*lane) : deluge::project::LaneView{};
}

void ParamCollection::forEachLaneAppended(
    ParamCollection& other, const ParamCollectionSummary& otherSummary, int32_t oldLength,
    int32_t reverseThisRepeatWithLength, bool pingpongingGenerally,
    deluge::function_ref<void(int32_t, const deluge::project::DocAutoParam&)> visit) {
	// The keys the repeat changes: those the other collection has nodes to repeat in
	std::vector<int32_t> appended;
	forEachAppendedKey(other, otherSummary, [&](int32_t key) {
		const deluge::project::DocAutoParam* src = other.findDocParamForId(key);
		if (src == nullptr || src->nodes.empty()) {
			return;
		}
		appended.push_back(key);
		const deluge::project::DocAutoParam* lane = findDocParamForId(key);
		deluge::project::DocAutoParam copy = lane != nullptr ? *lane : deluge::project::DocAutoParam{};
		deluge::project::appendParam(copy, *src, oldLength, reverseThisRepeatWithLength, pingpongingGenerally);
		if (!copy.nodes.empty()) {
			visit(key, copy);
		}
	});
	forEachLane([&](int32_t key, const deluge::project::DocAutoParam& lane) {
		if (std::ranges::find(appended, key) == appended.end()) {
			visit(key, lane);
		}
	});
}

ParamCollection::~ParamCollection() {
	deluge::modulation::dropWorkingCopies(*this);
	deluge::modulation::LanePublication::gone(*this);
}

void ParamCollection::notifyParamModifiedInSomeWay(ModelStackWithAutoParam const* modelStack, int32_t oldValue,
                                                   bool automationChanged, bool automatedBefore, bool automatedNow) {

	bool currentValueChanged = (oldValue != modelStack->autoParam->getCurrentValue());
	if (currentValueChanged || automationChanged) {
		modelStack->paramManager->notifyParamModifiedInSomeWay(modelStack, currentValueChanged, automationChanged,
		                                                       automatedNow);
	}

	if (automationChanged && automatedNow) {
		ticksTilNextEvent = 0;
	}

	// The interaction tier's writes only: a render writer (automation playback) never lifts a delay from OFF, and an
	// Edit made from the render would allocate there. The host renders on a thread its audio-context check does not
	// see, so the render block is checked too.
	if (!deluge_in_audio_context() && !deluge::memory::inRenderBlock()) {
		// A playhead move holds the render off, and the delay's reaction may grow its reserve or log an Edit: it runs
		// once the move's section ends.
		if (deluge::playback::MoveEffects::collecting()
		    && deluge::model::delayParamRole(*modelStack) != deluge::model::DelayParamRole::NONE
		    && deluge::playback::MoveEffects::defer({*modelStack, oldValue, automatedBefore})) {
			return;
		}
		deluge::model::onDelayParamModified(*modelStack, oldValue, automatedBefore);
	}
}

bool ParamCollection::mayParamInterpolate(int32_t paramId) {
	return true;
}

int32_t ParamCollection::paramValueToKnobPos(int32_t paramValue, ModelStackWithAutoParam* modelStack) {
	return deluge::plugin::to_knob(deluge::plugin::MappingKind::STANDARD, paramValue);
}

int32_t ParamCollection::knobPosToParamValue(int32_t knobPos, ModelStackWithAutoParam* modelStack) {
	return deluge::plugin::from_knob(deluge::plugin::MappingKind::STANDARD, knobPos);
}

void ParamCollection::setPlayPos(uint32_t pos, ModelStackWithParamCollection* modelStack, bool reversed) {
	ticksTilNextEvent = 0;
}

void ParamCollection::notifyPingpongOccurred(ModelStackWithParamCollection* modelStack) {
	ticksTilNextEvent = 0;
}
