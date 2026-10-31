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

/// @file
/// @brief The compressor subtree's exported root span. The `Node` literals are in `sound_compressor.h`.

#include "scenes/menu/nodes/sound_compressor.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "engines/effects/compressor.h" // the dsp-next display functions
#include "foundation/string_format.h"

#include <algorithm>
#include <cstring>

#include <limits>

namespace deluge::scenes::menu::nodes {

namespace {
constexpr const Node* kRoots[] = {&audio_comp};
} // namespace

const std::span<const Node* const> kSoundCompressorChildren{kRoots};

namespace {

/// The record's value behind knob position @p value: the stored q31 when it is at that position, else what
/// `writeCurrentValue` stores there (`compressor_values.h:31-36`; Blend's own at `:121-126`).
int32_t shown_q31(const Node& node, const MenuContext& ctx, int32_t value, bool blend) {
	if (const std::optional<int32_t> stored = bindings::stored(node.bind.target, ctx);
	    stored && (*stored >> 24) == value) {
		return *stored;
	}
	if (value >= kMaxKnobPos) {
		return blend ? std::numeric_limits<int32_t>::max() : (kMaxKnobPos - 1) << 24;
	}
	return value << 24;
}

ValueText compressor_text(CompressorShown which, const MenuContext& ctx, int32_t value, std::span<char> out) {
	namespace fx = deluge::dsp::fixed;
	float display = 0;
	int32_t places = 2;
	const char* unit = "MS";
	switch (which) {
	case CompressorShown::Attack:
		display = fx::compressor_attack_ms(shown_q31(comp_attack, ctx, value, false));
		break;
	case CompressorShown::Release:
		display = fx::compressor_release_ms(shown_q31(comp_release, ctx, value, false));
		places = 1;
		break;
	case CompressorShown::Ratio:
		display = fx::compressor_ratio(shown_q31(comp_ratio, ctx, value, false));
		unit = " : 1";
		break;
	case CompressorShown::SidechainHpf:
		display = fx::compressor_sidechain_hz(shown_q31(comp_hpf, ctx, value, false));
		unit = "HZ";
		break;
	case CompressorShown::Blend:
		display = static_cast<float>(fx::compressor_blend_percent(shown_q31(comp_blend, ctx, value, true)));
		places = 0;
		unit = " %";
		break;
	}
	char buf[24];
	floatToString(display, buf, places, places);
	size_t len = std::strlen(buf);
	for (const char* u = unit; *u != 0 && len + 1 < sizeof(buf); u++) {
		buf[len++] = *u;
	}
	const size_t n = std::min(len, out.size());
	std::memcpy(out.data(), buf, n);
	return ValueText{.length = n};
}

} // namespace

template <CompressorShown kWhich>
ValueText compressor_banner(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out) {
	return use == ValueTextUse::Notification ? compressor_text(kWhich, ctx, value, out) : kValueTextFallback;
}

template <CompressorShown kWhich>
ValueText compressor_screen(const MenuContext& ctx, int32_t value, ValueTextUse, std::span<char> out) {
	// `drawDecimal` keeps four characters of the unit (`decimal.cpp:213`); every unit here is at most four.
	return compressor_text(kWhich, ctx, value, out);
}

template ValueText compressor_banner<CompressorShown::Attack>(const MenuContext&, int32_t, ValueTextUse,
                                                              std::span<char>);
template ValueText compressor_banner<CompressorShown::Release>(const MenuContext&, int32_t, ValueTextUse,
                                                               std::span<char>);
template ValueText compressor_banner<CompressorShown::Ratio>(const MenuContext&, int32_t, ValueTextUse,
                                                             std::span<char>);
template ValueText compressor_banner<CompressorShown::SidechainHpf>(const MenuContext&, int32_t, ValueTextUse,
                                                                    std::span<char>);
template ValueText compressor_banner<CompressorShown::Blend>(const MenuContext&, int32_t, ValueTextUse,
                                                             std::span<char>);
template ValueText compressor_screen<CompressorShown::Attack>(const MenuContext&, int32_t, ValueTextUse,
                                                              std::span<char>);
template ValueText compressor_screen<CompressorShown::Release>(const MenuContext&, int32_t, ValueTextUse,
                                                               std::span<char>);
template ValueText compressor_screen<CompressorShown::Ratio>(const MenuContext&, int32_t, ValueTextUse,
                                                             std::span<char>);
template ValueText compressor_screen<CompressorShown::SidechainHpf>(const MenuContext&, int32_t, ValueTextUse,
                                                                    std::span<char>);
template ValueText compressor_screen<CompressorShown::Blend>(const MenuContext&, int32_t, ValueTextUse,
                                                             std::span<char>);

} // namespace deluge::scenes::menu::nodes
