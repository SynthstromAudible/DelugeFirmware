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
#include "modulation/params/param_value.h"
#include <cstdint>

class Serializer;
class Deserializer;
class ParamNode;

// For backing up a snapshot
class AutoParamState {
public:
	// Existing public curve-editing interface; this object owns its nodes.
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	LazyParamNodeVector nodes;
	int32_t value = 0;
	int64_t clipValue = 0;
	int64_t curveValue = 0;
	int32_t nodeBase = 0;
};

// Independently owned parameter data and algorithms, shared by firmware and unit tests.
// A live AutoParam privately inherits this storage and exposes only a const view.
class AutoParamData {
public:
	AutoParamData();
	void init();
	int32_t getValueAtPos(uint32_t pos, int32_t loopLength, bool reversed = false);
	bool grabValueFromPos(uint32_t pos, int32_t loopLength);
	void cloneFrom(AutoParamData* otherParam, bool copyAutomation);
	Error copyNodesForTrim(int32_t num_nodes, LazyParamNodeVector& trimmed) const;
	void deleteAutomationBasicForSetup();
	// Returns the value written, so preset saving can record it under a stable identity.
	int32_t writeToFile(Serializer& writer, bool writeAutomation, const int32_t* valueForOverride = nullptr);
	Error readFromFile(Deserializer& reader, int32_t readAutomationUpToPos);
	/// @brief Make sure the value of this AutoParam will be \p value at time \p pos.
	///
	/// If no node exists at the provided position, a new node will be created.
	///
	/// @param[in] pos Position to modify.
	/// @param[in] value Value to use.
	/// @param[in] shouldInterpolate Sets \ref "ParamNode::interpolated" on the created or modified node.
	///
	/// @return the index in to \ref nodes of the modified node.
	int32_t setNodeAtPos(int32_t pos, int32_t value, bool shouldInterpolate);
	[[nodiscard]] int32_t getCurrentValue() const {
		// Test the clip value itself, not just its binding tag, so no null pointer is ever dereferenced.
		const ClipParamValue* clip = clipValue();
		if (clip == nullptr) {
			return standaloneValue();
		}
		const ParamNodeVector* curve = nodes.get();
		if (curve != nullptr && curve->value.isCurrent()) [[likely]] {
			return curve->value.currentValue();
		}
		int32_t base = clip->getValue();
		return curve != nullptr ? curve->value.getValueForBase(base) : base;
	}
	// The value of a parameter as read during rendering. An automated parameter whose
	// baseline has not changed since its last evaluation reads only its curve.
	[[nodiscard]] int32_t getValueForRender() const {
		if (hasClipValueBinding()) [[likely]] {
			const ParamNodeVector* curve = nodes.get();
			if (curve == nullptr) {
				return clipValue()->getValue();
			}
			if (curve->value.isCurrent()) {
				return curve->value.currentValue();
			}
		}
		return evaluateValueForRender();
	}
	[[nodiscard]] int32_t getValueForClip(int32_t clip_value) const {
		auto* curve = nodes.get();
		return curve != nullptr && curve->getNumElements() != 0 ? curve->value.getValueForBase(clip_value) : clip_value;
	}
	[[nodiscard]] int32_t getClipValue() const {
		return (clipValue() != nullptr) ? clipValue()->getValue() : standaloneValue();
	}
	[[nodiscard]] int64_t getAutomationOffset() const {
		return (clipValue() != nullptr) && (nodes.get() != nullptr) ? nodes.get()->value.getOffset() : 0;
	}
	void bindClipValue(ClipParamValue* clip);
	[[nodiscard]] int64_t getUnclampedClipValue() const {
		return clipValue() != nullptr ? clipValue()->getUnclampedValue() : standaloneValue();
	}
	void captureState(AutoParamState& state, bool stealNodes);
	void swapStateBasicForSetup(AutoParamState* state);
	void rebindClipValueAfterMove(ClipParamValue* clip) {
		// Any curve evaluated through the old binding must evaluate again.
		parameterBaselinesChanged();
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		binding_.clip = clip;
		if (clip == nullptr) {

			setStandaloneValue(0);
		}
		nodes.setHasClipValue(clip != nullptr);
	}
	int32_t getNodeValue(const ParamNode* node) const;
	void setNodeValue(ParamNode* node, int32_t value);
	void restoreClipValueFromFile(int32_t value);
	void writeRelativeToFile(Serializer& writer) const;
	bool readRelativeFromFile(Deserializer& reader);
	void clearAutomationOffset() {
		if (nodes.get() != nullptr) {
			nodes.get()->value.setOffsetForBase(0, getNodeBase());
		}
	}
	void setCurrentValueBasicForSetup(int32_t value);
	[[nodiscard]] bool isAutomated() const { return nodes.getNumElements() != 0; }
	void cancelOverriding() { setOverridingTime(0); } // Will also cancel "latching".
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
	// Storage layout is fixed: one tagged curve pointer plus the scalar-binding union.
	/// The nodes that make up this parameter, and its playback state. If empty, the parameter reads its
	/// clip or standalone value.
	// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	LazyParamNodeVector nodes;
	[[nodiscard]] int64_t getCurveValue() const {
		return (clipValue() != nullptr) ? getAutomationOffset() : standaloneValue();
	}
	[[nodiscard]] int32_t getNodeBase() const { return (clipValue() != nullptr) ? clipValue()->getValue() : 0; }
	void restoreClipValue(int32_t value);

	union Binding {
		int32_t standalone = 0;
		ClipParamValue* clip;
		// NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes)
	} binding_;
	[[nodiscard]] int32_t standaloneValue() const {

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		return binding_.standalone;
	}
	void setStandaloneValue(int32_t value) {

		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		binding_.standalone = value;
	}
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
	[[nodiscard]] ClipParamValue* clipValue() const { return nodes.hasClipValue() ? binding_.clip : nullptr; }
	[[nodiscard]] bool hasClipValueBinding() const { return nodes.hasClipValue(); }
	[[nodiscard]] int32_t evaluateValueForRender() const;
	[[nodiscard]] int32_t resolveOffset(int64_t offset) const {
		return ClipParamValue::resolve(int64_t{getNodeBase()} + offset);
	}
	void setCurveValue(int64_t offset) {
		if (clipValue() == nullptr) {
			setStandaloneValue(ClipParamValue::resolve(offset));
		}
		else if (nodes.get() != nullptr) {
			nodes.get()->value.setOffsetForBase(offset, getNodeBase());
		}
	}
	[[nodiscard]] uint32_t getOverridingTime() const {
		return (nodes.get() != nullptr) ? nodes.get()->renewedOverridingAtTime : 0;
	}
	void setOverridingTime(uint32_t time) const {
		if (nodes.get() != nullptr) {
			nodes.get()->renewedOverridingAtTime = time;
		}
	}
	void rebaseNodes(int64_t difference);
	int64_t getOffsetAtPos(uint32_t pos, int32_t loopLength, bool reversed = false);
	Error readAutomationFromFile(Deserializer& reader, int32_t readAutomationUpToPos);
	Error readLegacyAutomationEndpoint(int32_t value, bool interpolated);
};
static_assert(sizeof(AutoParamData) == 2 * sizeof(void*));
