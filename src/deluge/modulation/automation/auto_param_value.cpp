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

AutoParamData::AutoParamData()
    : currentValue(0), valueIncrementPerHalfTick(0), interpolation_increment_remainder_float(0.0f),
      renewedOverridingAtTime(0) {
	init();
}

void AutoParamData::init() {
	nodes.init();
}

void AutoParamData::cloneFrom(AutoParamData* otherParam, bool copyAutomation) {
	if (copyAutomation) {
		nodes.cloneFrom(&otherParam->nodes);
	}
	else {
		nodes.init();
	}
	currentValue = otherParam->currentValue;
	resetInterpolationIncrement();
	renewedOverridingAtTime = 0;
}

// Beware. As this will do no notifying, the caller must ensure that any required notification is done.
// I.e. a ParamSet must be notified if automation is deleted.
void AutoParamData::deleteAutomationBasicForSetup() {
	nodes.empty();

	resetInterpolationIncrement();
	renewedOverridingAtTime = 0;
}

/// resets interpolation increment to zero (can happen if we reached parameter value limits)
/// or if we've reached another node in which case we'll be re-calculating the increment
void AutoParamData::resetInterpolationIncrement() {
	// Reset initializes the integer member; the interpolation mode selects subsequent accesses.
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
	valueIncrementPerHalfTick = 0;
	interpolation_increment_remainder_float = 0.0f;
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
	our_node->value = value;
	our_node->interpolated = shouldInterpolate;

	return i;
}

// The reason for specifying whether we're reversing is that at the exact pos of a non-interpolating node, where the
// value abruptly changes, well whether we want the value to the left or the right depends on which direction we're
// going.
int32_t AutoParamData::getValueAtPos(uint32_t pos, int32_t loopLength, bool reversed) {

	if (nodes.getNumElements() == 0) {
		return currentValue;
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
		return left_node->value;
	}

	int32_t ticks_since_left_node = static_cast<int32_t>(pos) - left_node->pos;
	if (ticks_since_left_node == 0) {
		return left_node->value;
	}

	if (ticks_since_left_node < 0) { // If pos we're looking at is left of leftmost...
		int32_t length_before_loop = loopLength;
		if (length_before_loop == 2147483647) { // If infinite length - and we know we're interpolating - well we'd have
			                                    // arrived at the next node value
			return right_node->value;
		}
		ticks_since_left_node += length_before_loop;
	}

	int32_t ticks_between_nodes = right_node->pos - left_node->pos;
	if (ticks_between_nodes <= 0) { // If pos we're looking at is right of rightmost...
		int32_t length_before_loop = loopLength;
		if (length_before_loop
		    == 2147483647) { // If infinite length, we'll still be on prev node's value, despite us interpolating
			return left_node->value;
		}
		ticks_between_nodes += length_before_loop;
	}

	int64_t distance = int64_t{right_node->value} - left_node->value;
	return static_cast<int32_t>(left_node->value + (distance * ticks_since_left_node / ticks_between_nodes));
}

// Returns whether a change was made to the current value.
bool AutoParamData::grabValueFromPos(uint32_t pos, int32_t loopLength) {
	if (nodes.getNumElements() == 0) {
		return false;
	}

	int32_t old_value = currentValue;
	currentValue = getValueAtPos(pos, loopLength);
	return (currentValue != old_value);
}

void AutoParamData::captureState(AutoParamState& state, bool stealNodes) {
	state.value = currentValue;
	// Either steal the nodes, leaving this parameter empty, or clone them.
	if (stealNodes) {
		state.nodes.swapStateWith(&nodes);
	}
	else {
		state.nodes.cloneFrom(&nodes);
	}
}
