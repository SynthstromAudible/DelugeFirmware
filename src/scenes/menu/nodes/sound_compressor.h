#pragma once

/// @brief The compressor subtree as data: `audio_comp` and its six columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains (fix round 1, I3). From
/// `gui/menu_item/generate/dmenus/compressor.py` and `generate/g_menus.inc:31-45` (at `ce4a79de0`):
///
// clang-format off
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `comp_threshold` | `threshold` (`:288`) | `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:26`) |
/// | `comp_ratio` | `compRatio` (`:289`) | `audio_compressor::Ratio` (`compressor_values.h:85`) |
/// | `comp_blend` | `compBlend` (`:290`) | `audio_compressor::Blend` (`compressor_values.h:108`) |
/// | `comp_attack` | `compAttack` (`:291`) | `audio_compressor::Attack` (`compressor_values.h:57`) |
/// | `comp_release` | `compRelease` (`:292`) | `audio_compressor::Release` (`compressor_values.h:70`) |
/// | `comp_hpf` | `compHPF` (`:293`) | `audio_compressor::SideHPF` (`compressor_values.h:95`) |
/// | `audio_comp` | `audioCompMenu` (`:302`) | `HorizontalMenu` |
// clang-format on
///
/// @note **`Scaling::HalfPrecision` on the threshold** -- `CompParam::readCurrentValue` /
///       `getFinalValue` (`compressor_params.h:29-33`) are the half-precision pair, the exception
///       `bindings.h` says a table must name at the call site because nothing can infer it. The
///       validator's `Flag::HalfPrecision` audit re-checks that it is set (`kHalfPrecisionNodes`).
///
/// @note **The five `CompressorValue`s are `Kind::Decimal` over the raw 0..`kMaxKnobPos` knob
///       position, not over the number legacy prints.** Legacy is a `DecimalWithoutScrolling`
///       (`compressor_values.h:18`) whose `getDisplayValue()` returns a derived float in real units
///       -- milliseconds, a ratio, hertz, a percentage (`:64`, `:77`, `:92`, `:104`, `:125`) -- with
///       a unit suffix, while its stored value is the knob position. `Binding` has a
///       `decimal_places` but no display transform and no unit, so this generation stores and edits
///       the knob position and renders it with legacy's own decimal-place count. Recorded as a known
///       gap for E2 (a `Binding` display hook), not a silent difference.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp" // kMaxKnobPos

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The five `RMSFeedbackCompressor` hooks. Bodies (and their citations) in the `.cpp`.
/// @{
int32_t get_comp_attack(const MenuContext& ctx);
void set_comp_attack(const MenuContext& ctx, int32_t value);
int32_t get_comp_release(const MenuContext& ctx);
void set_comp_release(const MenuContext& ctx, int32_t value);
int32_t get_comp_ratio(const MenuContext& ctx);
void set_comp_ratio(const MenuContext& ctx, int32_t value);
int32_t get_comp_side_hpf(const MenuContext& ctx);
void set_comp_side_hpf(const MenuContext& ctx, int32_t value);
int32_t get_comp_blend(const MenuContext& ctx);
void set_comp_blend(const MenuContext& ctx, int32_t value);
/// @}

/// `CompressorValue::getMaxValue()` (`compressor_values.h:51`) over `Number`'s default minimum of 0
/// (`gui/menu_item/number.h:31`).
inline constexpr IntRange kKnobRange{0, kMaxKnobPos, 1};

inline constexpr Node comp_threshold = param(
    sid(deluge::l10n::String::STRING_FOR_THRESHOLD),
    {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_COMPRESSOR_THRESHOLD},
    Hint::Bar, nullptr, sid(deluge::l10n::String::STRING_FOR_COMP_THRESHOLD_MENU_TITLE), Scaling::HalfPrecision);
// The decimal-place counts are `compressor_values.h:52`, `:78` and `:127`; the hints are
// `getRenderingStyle()` at `:54`, `:65`, `:79` and `:106`.
inline constexpr Node comp_ratio =
    with_hint(decimal_item(sid(deluge::l10n::String::STRING_FOR_RATIO), get_comp_ratio, set_comp_ratio, kKnobRange, 2),
              Hint::Knob);
inline constexpr Node comp_blend =
    with_hint(decimal_item(sid(deluge::l10n::String::STRING_FOR_BLEND), get_comp_blend, set_comp_blend, kKnobRange, 0),
              Hint::Knob);
inline constexpr Node comp_attack = with_hint(
    decimal_item(sid(deluge::l10n::String::STRING_FOR_ATTACK), get_comp_attack, set_comp_attack, kKnobRange, 2),
    Hint::Attack);
inline constexpr Node comp_release = with_hint(
    decimal_item(sid(deluge::l10n::String::STRING_FOR_RELEASE), get_comp_release, set_comp_release, kKnobRange, 1),
    Hint::Release);
inline constexpr Node comp_hpf = with_hint(
    decimal_item(sid(deluge::l10n::String::STRING_FOR_HPF), get_comp_side_hpf, set_comp_side_hpf, kKnobRange, 2),
    Hint::Hpf);

inline constexpr const Node* kCompChildren[] = {&comp_threshold, &comp_ratio,   &comp_blend,
                                                &comp_attack,    &comp_release, &comp_hpf};
inline constexpr Node audio_comp = submenu(sid(deluge::l10n::String::STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR),
                                           kCompChildren, Layout::Horizontal);

/// @brief This file's contribution to `kBatch1Roots`: `audioCompMenu` (`g_menus.inc:45`).
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundCompressorChildren;

} // namespace deluge::scenes::menu::nodes
