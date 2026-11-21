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

#pragma once
#include "definitions_cxx.hpp"
#include "modulation/params/param_node_vector.h"
#include <cstdint>

class Serializer;
class Deserializer;
class ParamNode;

// For backing up a snapshot
class AutoParamState {
public:
	LazyParamNodeVector nodes;
	int32_t value;
};

// Independently owned parameter data and algorithms, shared by firmware and unit tests.
// A live AutoParam privately inherits this storage and exposes only a const view.
class AutoParamData {
public:
	AutoParamData();
	void init();
	void captureState(AutoParamState& state, bool stealNodes);
	int32_t getValueAtPos(uint32_t pos, int32_t loopLength, bool reversed = false);
	bool grabValueFromPos(uint32_t pos, int32_t loopLength);
	void cloneFrom(AutoParamData* otherParam, bool copyAutomation);
	void deleteAutomationBasicForSetup();
	void writeToFile(Serializer& writer, bool writeAutomation, const int32_t* valueForOverride = nullptr);
	Error readFromFile(Deserializer& reader, int32_t readAutomationUpToPos);
	int32_t setNodeAtPos(int32_t pos, int32_t value, bool shouldInterpolate);
	[[nodiscard]] int32_t getCurrentValue() const { return currentValue; }
	void setCurrentValueBasicForSetup(int32_t value) { currentValue = value; }
	[[nodiscard]] bool isAutomated() const { return nodes.getNumElements() != 0; }
	void cancelOverriding() { renewedOverridingAtTime = 0; }
	void resetInterpolationIncrement();
	// Standalone storage owners may edit their own data. AutoParam inherits privately
	// and exposes only a const data view, so this cannot mutate a live parameter.
	LazyParamNodeVector& editNodes() { return nodes; }
	[[nodiscard]] int32_t nodeCount() const { return nodes.getNumElements(); }
	[[nodiscard]] const ParamNode* nodeAt(int32_t index) const { return nodes.getElement(index); }
	[[nodiscard]] const void* nodeStorageIdentity() const { return nodes.get(); }
	[[nodiscard]] int32_t searchNodes(int32_t key, int32_t comparison) const {
		return nodes.get() != nullptr ? nodes.get()->search(key, comparison) : comparison;
	}

protected:
	// Runtime methods need direct access; one access level preserves standard-layout storage.
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	LazyParamNodeVector nodes;
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	int32_t currentValue;
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	union {
		int32_t valueIncrementPerHalfTick;
		float value_increment_per_half_tick_float;
	};
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	float interpolation_increment_remainder_float;
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	uint32_t renewedOverridingAtTime;
	Error readAutomationFromFile(Deserializer& reader, int32_t readAutomationUpToPos);
	Error readLegacyAutomationEndpoint(int32_t value, bool interpolated);
};
