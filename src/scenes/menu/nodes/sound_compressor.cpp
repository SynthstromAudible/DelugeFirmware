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
/// @brief The compressor subtree's `get`/`set` hooks, its exported root span and its expected
///        half-precision set. The `Node` literals are in `sound_compressor.h`.

#include "scenes/menu/nodes/sound_compressor.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"

#include "model/mod_controllable/mod_controllable_audio.h"

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief `lshiftAndSaturate<24>` (`util/functions.h:103-106`), transcribed.
///
/// `signed_saturate<8>(v) << 24` -- the device spelling is an `ssat` instruction
/// (`util/fixedpoint.h:106-112`), the host one the same clamp in C++ (`:197-202`). Transcribed
/// rather than included because `util/` is outside what `src/scenes` may reach
/// (`scripts/check_layer_rules.py`'s `scenes-includes-outside`).
constexpr int32_t lshift_saturate_24(int32_t v) {
	constexpr int32_t kHi = (1 << 7) - 1; // 127
	constexpr int32_t kLo = -kHi - 1;     // -128
	const int32_t clamped = v > kHi ? kHi : (v < kLo ? kLo : v);
	return clamped << 24;
}

/// @brief `CompressorValue::readCurrentValue` (`compressor_values.h:20-23`): the knob position is
///        the stored q31 shifted down by 24.
int32_t knob_pos(int64_t stored) {
	return static_cast<int32_t>(static_cast<uint64_t>(stored) >> 24);
}

/// @brief `CompressorValue::writeCurrentValue`'s clamp + shift (`compressor_values.h:24-32`).
int32_t to_q31(int32_t value) {
	return lshift_saturate_24(value >= kMaxKnobPos ? kMaxKnobPos - 1 : value);
}

} // namespace

int32_t get_comp_attack(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getAttack()) : 0; // `compressor_values.h:60-62`
}
void set_comp_attack(const MenuContext& ctx, int32_t value) {
	// `CompressorValue::writeCurrentValue`'s affect-entire loop (`compressor_values.h:33-47`).
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setAttack(to_q31(v));
	                           });
}

int32_t get_comp_release(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getRelease()) : 0; // `compressor_values.h:73-75`
}
void set_comp_release(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setRelease(to_q31(v));
	                           });
}

int32_t get_comp_ratio(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getRatio()) : 0; // `compressor_values.h:88-90`
}
void set_comp_ratio(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setRatio(to_q31(v));
	                           });
}

int32_t get_comp_side_hpf(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getSidechain()) : 0; // `compressor_values.h:98-100`
}
void set_comp_side_hpf(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setSidechain(to_q31(v));
	                           });
}

int32_t get_comp_blend(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getBlend()) : 0; // `compressor_values.h:111`
}
void set_comp_blend(const MenuContext& ctx, int32_t value) {
	// `Blend::setCompressorValue` (`compressor_values.h:112-126`) ignores the q31 it is handed and
	// re-derives one, saturating to a full `ONE_Q31` at the top of the range rather than to 127<<24.
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setBlend(v < kMaxKnobPos ? lshift_saturate_24(v) : ONE_Q31);
	                           });
}

namespace {
constexpr const Node* kRoots[] = {&audio_comp};
} // namespace

const std::span<const Node* const> kSoundCompressorChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
