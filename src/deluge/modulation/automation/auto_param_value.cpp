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

#include "modulation/automation/auto_param_data.h"
#include "modulation/params/param_node.h"

AutoParamData::AutoParamData() {
	init();
	setCurrentValueBasicForSetup(0);
	resetInterpolationIncrement();
}

void AutoParamData::init() {
	nodes.init();
}

int32_t AutoParamData::getNodeValue(const ParamNode* node) const {
	return resolveOffset(node->offset);
}

void AutoParamData::setNodeValue(ParamNode* node, int32_t value) {
	node->offset = int64_t{value} - getNodeBase();
}

void AutoParamData::rebaseNodes(int64_t difference) {
	if (difference == 0) {
		return;
	}
	for (int32_t i = 0; i < nodes.getNumElements(); ++i) {
		nodes.getElement(i)->offset += difference;
	}
}

int32_t AutoParamData::evaluateValueForRender() const {
	const ClipParamValue* clip = clipValue();
	if (clip == nullptr) {
		return standaloneValue();
	}
	int32_t base = clip->getValue();
	const ParamNodeVector* curve = nodes.get();
	if (curve == nullptr || nodes.getNumElements() == 0) {
		return base;
	}
	int32_t value = curve->value.getValueForBase(base);
	curve->value.markCurrent();
	return value;
}

void AutoParamData::bindClipValue(ClipParamValue* clip) {
	int32_t old_base = getNodeBase();
	rebindClipValueAfterMove(clip);
	rebaseNodes(int64_t{old_base} - getNodeBase());
}

PLACE_SDRAM_TEXT void AutoParamData::restoreClipValueFromFile(int32_t value) {
	int32_t old_base = getNodeBase();
	restoreClipValue(value);
	rebaseNodes(int64_t{old_base} - getNodeBase());
}

PLACE_SDRAM_TEXT void AutoParamData::cloneFrom(AutoParamData* otherParam, bool copyAutomation) {
	if (copyAutomation) {
		nodes.cloneFrom(&otherParam->nodes);
	}
	else {
		nodes.init();
	}
	restoreClipValue(otherParam->getClipValue());
	rebaseNodes(int64_t{otherParam->getNodeBase()} - getNodeBase());
	setCurrentValueBasicForSetup(otherParam->getCurrentValue());
	if (isAutomated()) {
		setCurveValue(otherParam->getCurveValue() + int64_t{otherParam->getNodeBase()} - getNodeBase());
	}
	if (clipValue() != nullptr) {
		clipValue()->setUnclampedValue(otherParam->getUnclampedClipValue());
	}
	resetInterpolationIncrement();
	setOverridingTime(0);
}

// Beware. As this will do no notifying, the caller must ensure that any required notification is done.
// I.e. a ParamSet must be notified if automation is deleted.
void AutoParamData::deleteAutomationBasicForSetup() {
	nodes.empty();
	clearAutomationOffset();
	resetInterpolationIncrement();
	setOverridingTime(0);
}

/// resets interpolation increment to zero (can happen if we reached parameter value limits)
/// or if we've reached another node in which case we'll be re-calculating the increment
void AutoParamData::resetInterpolationIncrement() {
	if (nodes.get() == nullptr) {
		return;
	}
	// Reset initializes the integer member; the interpolation mode selects subsequent accesses.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
	nodes.get()->valueIncrementPerHalfTick = 0;
	nodes.get()->interpolation_increment_remainder_float_ = 0.0f;
}

int32_t AutoParamData::setNodeAtPos(int32_t pos, int32_t value, bool shouldInterpolate) {
	int32_t i = nodes.search(pos, GREATER_OR_EQUAL);
	ParamNode* our_node = nullptr;

	// Insert a node unless there's already one at pos
	if (i < nodes.getNumElements()) {
		our_node = nodes.getElement(i);
	}
	if (our_node == nullptr || our_node->pos != pos) {
		Error error = nodes.insertAtIndex(i);
		if (error != Error::NONE) {
			return -1;
		}
		our_node = nodes.getElement(i);
		our_node->pos = pos;
	}
	setNodeValue(our_node, value);
	our_node->interpolated = shouldInterpolate;

	return i;
}

// The reason for specifying whether we're reversing is that at the exact pos of a non-interpolating node, where the
// value abruptly changes, well whether we want the value to the left or the right depends on which direction we're
// going.
int32_t AutoParamData::getValueAtPos(uint32_t pos, int32_t loopLength, bool reversed) {
	return resolveOffset(getOffsetAtPos(pos, loopLength, reversed));
}

int64_t AutoParamData::getOffsetAtPos(uint32_t pos, int32_t loopLength, bool reversed) {

	if (nodes.getNumElements() == 0) {
		return getCurveValue();
	}

	int32_t right_i = nodes.search(static_cast<int32_t>(pos) + static_cast<int32_t>(!reversed), GREATER_OR_EQUAL);
	if (right_i >= nodes.getNumElements()) {
		right_i = 0;
	}
	ParamNode* right_node = nodes.getElement(right_i);

	int32_t left_i = right_i - 1;
	if (left_i < 0) {
		left_i += nodes.getNumElements();
	}
	ParamNode* left_node = nodes.getElement(left_i);
	if (!right_node->interpolated) {
		return left_node->offset;
	}

	int32_t ticks_since_left_node = static_cast<int32_t>(pos) - left_node->pos;
	if (ticks_since_left_node == 0) {
		return left_node->offset;
	}

	if (ticks_since_left_node < 0) { // If pos we're looking at is left of leftmost...
		int32_t length_before_loop = loopLength;
		if (length_before_loop == 2147483647) { // If infinite length - and we know we're interpolating - well we'd have
			                                    // arrived at the next node value
			return right_node->offset;
		}
		ticks_since_left_node += length_before_loop;
	}

	int32_t ticks_between_nodes = right_node->pos - left_node->pos;
	if (ticks_between_nodes <= 0) { // If pos we're looking at is right of rightmost...
		int32_t length_before_loop = loopLength;
		if (length_before_loop
		    == 2147483647) { // If infinite length, we'll still be on prev node's value, despite us interpolating
			return left_node->offset;
		}
		ticks_between_nodes += length_before_loop;
	}

	return interpolateAutomationOffset(left_node->offset, right_node->offset, ticks_since_left_node,
	                                   ticks_between_nodes);
}

// Returns whether a change was made to the current value.
bool AutoParamData::grabValueFromPos(uint32_t pos, int32_t loopLength) {
	if (nodes.getNumElements() == 0) {
		return false;
	}

	int32_t old_value = getCurrentValue();
	setCurveValue(getOffsetAtPos(pos, loopLength));
	return (getCurrentValue() != old_value);
}

void AutoParamData::setCurrentValueBasicForSetup(int32_t value) {
	if (clipValue() == nullptr) {
		setStandaloneValue(value);
	}
	else if (isAutomated()) {
		int32_t base = getNodeBase();
		nodes.get()->value.setOffsetForBase(int64_t{value} - base, base);
	}
	else {
		clipValue()->setValue(value);
		clearAutomationOffset();
	}
}
void AutoParamData::restoreClipValue(int32_t value) {
	if (clipValue() != nullptr) {
		int32_t current = getCurrentValue();
		clipValue()->setValue(value);
		setCurveValue(int64_t{current} - value);
	}
}

void AutoParamData::captureState(AutoParamState& state, bool stealNodes) {
	state.value = getCurrentValue();
	state.clipValue = getUnclampedClipValue();
	state.curveValue = getCurveValue();
	state.nodeBase = getNodeBase();
	// Either steal the nodes, leaving this parameter empty, or clone them.
	if (stealNodes) {
		state.nodes.swapStateWith(&nodes);
	}
	else {
		state.nodes.cloneFrom(&nodes);
	}
}

void AutoParamData::swapStateBasicForSetup(AutoParamState* state) {
	int32_t old_value_here = getCurrentValue();
	int64_t old_clip_value = getUnclampedClipValue();
	int64_t old_curve_value = getCurveValue();
	int32_t old_base = getNodeBase();
	// Undo exchanges curve data, not the live transport's interpolation/override
	// context, which stays with the parameter.
	int64_t increment_bits = 0;
	float remainder = 0;
	uint32_t overriding = getOverridingTime();
	if (nodes.get() != nullptr) {
		// Copy the active integer/float representation without interpreting it.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		std::memcpy(&increment_bits, &nodes.get()->valueIncrementPerHalfTick, sizeof(increment_bits));
		remainder = nodes.get()->interpolation_increment_remainder_float_;
	}
	nodes.swapStateWith(&state->nodes);
	if (nodes.get() != nullptr) {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		std::memcpy(&nodes.get()->valueIncrementPerHalfTick, &increment_bits, sizeof(increment_bits));
		nodes.get()->interpolation_increment_remainder_float_ = remainder;
		setOverridingTime(overriding);
	}
	if (clipValue() != nullptr) {
		clipValue()->setUnclampedValue(state->clipValue);
	}
	else {
		setStandaloneValue(state->value);
	}
	if (isAutomated()) {
		int64_t difference = int64_t{state->nodeBase} - getNodeBase();
		rebaseNodes(difference);
		setCurveValue(state->curveValue + difference);
	}
	state->value = old_value_here;
	state->clipValue = old_clip_value;
	state->curveValue = old_curve_value;
	state->nodeBase = old_base;
}

Error AutoParamData::copyNodesForTrim(int32_t num_nodes, LazyParamNodeVector& trimmed) const {
	Error error = trimmed.insertAtIndex(0, num_nodes);
	if (error != Error::NONE) {
		return error;
	}
	for (int32_t i = 0; i < num_nodes; ++i) {
		*trimmed.getElement(i) = *nodes.getElement(i);
	}
	// Trimming changes the nodes, not the live value, interpolation or override.
	// Copy before undo can take ownership of the original vector.
	trimmed.get()->copyPlaybackStateFrom(*nodes.get());
	return Error::NONE;
}
