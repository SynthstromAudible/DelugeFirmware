/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// A verbatim copy of the descriptor class the runtime's cable set used for its words, renamed, with the synth's
// PatchSource and a plain cast for util::to_underlying: the oracle synth::CableTarget's word operations are checked
// against.
#pragma once

#include "synth/modulation/patch_source.h"

#include <cstdint>

namespace param_descriptor_reference {

using PatchSource = deluge::synth::PatchSource;

class ParamDescriptorReference {
public:
	ParamDescriptorReference() = default;
	ParamDescriptorReference(const ParamDescriptorReference&) = default;
	ParamDescriptorReference(ParamDescriptorReference&&) = default;
	ParamDescriptorReference& operator=(const ParamDescriptorReference&) = default;
	ParamDescriptorReference& operator=(ParamDescriptorReference&&) = default;

	/// Construct a ParamDescriptorReference directly from its value.
	explicit ParamDescriptorReference(int32_t data_) : data(data_) {}

	constexpr void setToHaveParamOnly(int32_t p) { data = p | 0xFFFFFF00; }

	constexpr void setToHaveParamAndSource(int32_t p, PatchSource s) {
		data = p | (static_cast<uint8_t>(s) << 8) | 0xFFFF0000;
	}

	constexpr void setToHaveParamAndTwoSources(int32_t p, PatchSource s, PatchSource sLowestLevel) {
		data = p | (static_cast<uint8_t>(s) << 8) | (static_cast<uint8_t>(sLowestLevel) << 16) | 0xFF000000;
	}

	[[nodiscard]] constexpr bool isSetToParamWithNoSource(int32_t p) const { return (data == (p | 0xFFFFFF00)); }

	[[nodiscard]] constexpr inline bool isSetToParamAndSource(int32_t p, PatchSource s) const {
		return (data == (p | (static_cast<uint8_t>(s) << 8) | 0xFFFF0000));
	}

	[[nodiscard]] constexpr bool isJustAParam() const { return (data & 0x0000FF00) == 0x0000FF00; }

	[[nodiscard]] constexpr int32_t getJustTheParam() const { return data & 0xFF; }

	constexpr void changeParam(int32_t newParam) { data = (data & 0xFFFFFF00) | newParam; }

	[[nodiscard]] constexpr PatchSource getBottomLevelSource() const { // As in, the one furthest away from the param.
		if ((data & 0x00FF0000) == 0x00FF0000) {
			return static_cast<PatchSource>((data >> 8) & 0xFF);
		}

		return static_cast<PatchSource>((data >> 16) & 0xFF);
	}

	constexpr void addSource(PatchSource newSource) {
		uint8_t newSourceUnderlying = static_cast<uint8_t>(newSource);
		if ((data & 0x0000FF00) == 0x0000FF00) {
			data = (data & 0xFFFF00FF) | (newSourceUnderlying << 8);
		}
		else if ((data & 0x00FF0000) == 0x00FF0000) {
			data = (data & 0xFF00FFFF) | (newSourceUnderlying << 16);
		}
		else {
			data = (data & 0x00FFFFFF) | (newSourceUnderlying << 24);
		}
	}

	[[nodiscard]] constexpr ParamDescriptorReference getDestination() const {
		ParamDescriptorReference newParamDescriptorReference{};
		if ((data & 0x00FF0000) == 0x00FF0000) {
			newParamDescriptorReference.data = data | 0x0000FF00;
		}
		else {
			newParamDescriptorReference.data = data | 0x00FF0000;
		}
		return newParamDescriptorReference;
	}

	[[nodiscard]] constexpr bool hasJustOneSource() const {
		return ((data & 0xFFFF0000) == 0xFFFF0000) && ((data & 0x0000FF00) != 0x0000FF00);
	}

	[[nodiscard]] constexpr PatchSource getTopLevelSource() const { // As in, the one, nearest the param.
		return static_cast<PatchSource>((data & 0x0000FF00) >> 8);
	}

	[[nodiscard]] constexpr PatchSource getSecondSourceFromTop() const {
		return static_cast<PatchSource>((data & 0x00FF0000) >> 16);
	}

	[[nodiscard]] constexpr bool hasSecondSource() const { return ((data & 0x00FF0000) != 0x00FF0000); }

	constexpr void setToNull() { data = 0xFFFFFFFF; }

	[[nodiscard]] constexpr bool isNull() const { return (data == 0xFFFFFFFF); }

	uint32_t data{0};
};

constexpr bool operator==(const ParamDescriptorReference& lhs, const ParamDescriptorReference& rhs) {
	return (lhs.data == rhs.data);
}
constexpr bool operator!=(const ParamDescriptorReference& lhs, const ParamDescriptorReference& rhs) {
	return !(lhs == rhs);
}

} // namespace param_descriptor_reference
