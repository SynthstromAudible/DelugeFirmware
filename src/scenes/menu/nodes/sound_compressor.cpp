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
/// @brief The compressor subtree: `audioCompMenu` and its six columns.
///
/// From `gui/menu_item/generate/dmenus/compressor.py` and `generate/g_menus.inc:31-45`:
///
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `comp_threshold` | `threshold` (`:31`) | `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:26`)
/// | | `comp_ratio` | `compRatio` (`:32`) | `audio_compressor::Ratio` (`compressor_values.h:88`) | | `comp_blend` |
/// `compBlend` (`:33`) | `audio_compressor::Blend` (`compressor_values.h:110`) | | `comp_attack` | `compAttack` (`:34`)
/// | `audio_compressor::Attack` (`compressor_values.h:57`) | | `comp_release` | `compRelease` (`:35`) |
/// `audio_compressor::Release` (`compressor_values.h:71`) | | `comp_hpf` | `compHPF` (`:36`) |
/// `audio_compressor::SideHPF` (`compressor_values.h:97`) | | `audio_comp` | `audioCompMenu` (`:45`) | `HorizontalMenu`
/// |
///
/// @note **`Scaling::HalfPrecision` on the threshold** -- `CompParam::readCurrentValue` /
///       `getFinalValue` (`compressor_params.h:29-33`) are the half-precision pair, the exception
///       `bindings.h` says a table must name at the call site because nothing can infer it. The
///       validator's `Flag::HalfPrecision` audit re-checks that it is set (`kHalfPrecisionNodes`).
///
/// @note **The five `CompressorValue`s are `Kind::Decimal` over the raw 0..`kMaxKnobPos` knob
///       position, not over the number legacy prints.** Legacy is a `DecimalWithoutScrolling`
///       (`compressor_values.h:20`) whose `getDisplayValue()` returns a derived float in real units
///       -- milliseconds, a ratio, hertz, a percentage (`:68`, `:82`, `:92`, `:102`, `:120`) -- with
///       a unit suffix, while its stored value is the knob position. `Binding` has a
///       `decimal_places` but no display transform and no unit, so this generation stores and edits
///       the knob position and renders it with legacy's own decimal-place count. Recorded as a known
///       gap for E2 (a `Binding` display hook), not a silent difference.

#include "scenes/menu/nodes/nodes.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp"
#include "model/mod_controllable/mod_controllable_audio.h"

#include <array>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;
namespace params = deluge::modulation::params;

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

/// @brief `CompressorValue::readCurrentValue` (`compressor_values.h:21-24`): the knob position is
///        the stored q31 shifted down by 24.
int32_t knob_pos(int64_t stored) {
	return static_cast<int32_t>(static_cast<uint64_t>(stored) >> 24);
}

/// @brief `CompressorValue::writeCurrentValue`'s clamp + shift (`compressor_values.h:26-30`).
int32_t to_q31(int32_t value) {
	return lshift_saturate_24(value >= kMaxKnobPos ? kMaxKnobPos - 1 : value);
}

int32_t get_attack(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getAttack()) : 0; // `compressor_values.h:60-62`
}
void set_attack(const MenuContext& ctx, int32_t value) {
	// `CompressorValue::writeCurrentValue`'s affect-entire loop (`compressor_values.h:31-47`).
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setAttack(to_q31(v));
	                           });
}

int32_t get_release(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getRelease()) : 0; // `compressor_values.h:74-76`
}
void set_release(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setRelease(to_q31(v));
	                           });
}

int32_t get_ratio(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getRatio()) : 0; // `compressor_values.h:91-93`
}
void set_ratio(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setRatio(to_q31(v));
	                           });
}

int32_t get_side_hpf(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getSidechain()) : 0; // `compressor_values.h:100-102`
}
void set_side_hpf(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setSidechain(to_q31(v));
	                           });
}

int32_t get_blend(const MenuContext& ctx) {
	return ctx.mod != nullptr ? knob_pos(ctx.mod->compressor.getBlend()) : 0; // `compressor_values.h:113`
}
void set_blend(const MenuContext& ctx, int32_t value) {
	// `Blend::setCompressorValue` (`compressor_values.h:114-126`) ignores the q31 it is handed and
	// re-derives one, saturating to a full `ONE_Q31` at the top of the range rather than to 127<<24.
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->compressor.setBlend(v < kMaxKnobPos ? lshift_saturate_24(v) : ONE_Q31);
	                           });
}

// ---------------------------------------------------------------------------------------------
// The tree. Range is `CompressorValue::getMaxValue()` (`compressor_values.h:51`) over `Number`'s
// default minimum of 0 (`gui/menu_item/number.h:31`); the decimal-place counts are `:52`, `:78`
// and `:127`; the hints are `getRenderingStyle()` at `:54`, `:65`, `:79` and `:106`.

constexpr IntRange kKnobRange{0, kMaxKnobPos, 1};

constexpr Node comp_threshold =
    param(sid(String::STRING_FOR_THRESHOLD), {params::Kind::UNPATCHED_SOUND, params::UNPATCHED_COMPRESSOR_THRESHOLD},
          Hint::Bar, nullptr, sid(String::STRING_FOR_COMP_THRESHOLD_MENU_TITLE), Scaling::HalfPrecision);
constexpr Node comp_ratio =
    with_hint(decimal_item(sid(String::STRING_FOR_RATIO), get_ratio, set_ratio, kKnobRange, 2), Hint::Knob);
constexpr Node comp_blend =
    with_hint(decimal_item(sid(String::STRING_FOR_BLEND), get_blend, set_blend, kKnobRange, 0), Hint::Knob);
constexpr Node comp_attack =
    with_hint(decimal_item(sid(String::STRING_FOR_ATTACK), get_attack, set_attack, kKnobRange, 2), Hint::Attack);
constexpr Node comp_release =
    with_hint(decimal_item(sid(String::STRING_FOR_RELEASE), get_release, set_release, kKnobRange, 1), Hint::Release);
constexpr Node comp_hpf =
    with_hint(decimal_item(sid(String::STRING_FOR_HPF), get_side_hpf, set_side_hpf, kKnobRange, 2), Hint::Hpf);

constexpr const Node* kCompChildren[] = {&comp_threshold, &comp_ratio,   &comp_blend,
                                         &comp_attack,    &comp_release, &comp_hpf};
constexpr Node audio_comp =
    submenu(sid(String::STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR), kCompChildren, Layout::Horizontal);

constexpr const Node* kRoots[] = {&audio_comp};
constexpr const Node* kHalfPrecision[] = {&comp_threshold};

} // namespace

const std::span<const Node* const> kSoundCompressorChildren{kRoots};
const std::span<const Node* const> kCompressorHalfPrecisionNodes{kHalfPrecision};

} // namespace deluge::scenes::menu::nodes
