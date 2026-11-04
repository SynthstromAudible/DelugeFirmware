#pragma once

/// @brief The compressor subtree as data: `audio_comp` and its six columns.
///
/// `inline constexpr` for the reason `sound_filters.h` explains. From
/// `gui/ui/menus.cpp`'s compressor block, from `threshold`:
///
// clang-format off
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `comp_threshold` | `threshold` | `audio_compressor::CompParam` (`audio_compressor/compressor_params.h:26`) |
/// | `comp_ratio` | `compRatio` | `audio_compressor::Ratio` |
/// | `comp_blend` | `compBlend` | `audio_compressor::Blend` |
/// | `comp_attack` | `compAttack` | `audio_compressor::Attack` |
/// | `comp_release` | `compRelease` | `audio_compressor::Release` |
/// | `comp_hpf` | `compHPF` | `audio_compressor::SideHPF` |
/// | `audio_comp` | `audioCompMenu` | `HorizontalMenu` |
// clang-format on
///
/// @note **`Scaling::HalfPrecision` on the threshold** -- `CompParam::readCurrentValue` /
///       `getFinalValue` (`audio_compressor::CompParam`) are the half-precision pair, the exception
///       `bindings.h` says a table must name at the call site because nothing can infer it. The
///       validator's `Flag::HalfPrecision` audit re-checks that it is set (`kHalfPrecisionNodes`).
///
/// @note **The five `CompressorValue`s store and edit the knob position** (`Kind::Decimal` over 0..`kMaxKnobPos`), as
///       legacy's `DecimalWithoutScrolling` does (`audio_compressor::CompressorValue`); their banners and screens show
///       `getDisplayValue()`'s real units through `compressor_banner`/`compressor_screen`.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"

#include "definitions_cxx.hpp" // kMaxKnobPos

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @brief Which `audio_compressor::CompressorValue` a hook formats (`compressor_values.h:68-134`).
enum class CompressorShown : uint8_t { Attack, Release, Ratio, SidechainHpf, Blend };

/// @brief `DecimalWithoutScrolling::getNotificationValue` (`decimal.cpp:221-225`): the display value with its places
///        and unit, for the banner only.
template <CompressorShown kWhich>
ValueText compressor_banner(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);
/// @brief `DecimalWithoutScrolling::drawPixelsForOled` (`decimal.cpp:217-219`): the same text, for the screen.
template <CompressorShown kWhich>
ValueText compressor_screen(const MenuContext& ctx, int32_t value, ValueTextUse use, std::span<char> out);

inline constexpr Node comp_threshold = param(
    sid(deluge::l10n::String::STRING_FOR_THRESHOLD),
    {deluge::modulation::params::Kind::UNPATCHED_SOUND, deluge::modulation::params::UNPATCHED_COMPRESSOR_THRESHOLD},
    Hint::Bar, nullptr, sid(deluge::l10n::String::STRING_FOR_COMP_THRESHOLD_MENU_TITLE), Scaling::HalfPrecision);
// The decimal-place counts are the `getNumDecimalPlaces()` overrides of `audio_compressor::CompressorValue`, `Release`
// and `Blend`; the hints are the `getRenderingStyle()` overrides of `CompressorValue`, `Attack`, `Release` and
// `SideHPF`.
inline constexpr Node comp_ratio = with_leaf_text(
    with_value_text(
        with_horizontal_step(
            with_hint(with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_RATIO), Kind::Decimal,
                                                       {.kind = deluge::scenes::bind::Target::Kind::EFFECT,
                                                        .path = "audioCompressor/ratio"}),
                                          2),
                      Hint::Knob),
            HorizontalStep{.coarse = 1, .fine = 1}),
        compressor_banner<CompressorShown::Ratio>),
    compressor_screen<CompressorShown::Ratio>);
inline constexpr Node comp_blend = with_leaf_text(
    with_value_text(
        with_horizontal_step(
            with_hint(with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_BLEND), Kind::Decimal,
                                                       {.kind = deluge::scenes::bind::Target::Kind::EFFECT,
                                                        .path = "audioCompressor/compBlend"}),
                                          0),
                      Hint::Knob),
            HorizontalStep{.coarse = 1, .fine = 1}),
        compressor_banner<CompressorShown::Blend>),
    compressor_screen<CompressorShown::Blend>);
inline constexpr Node comp_attack = with_leaf_text(
    with_value_text(
        with_horizontal_step(
            with_column_name(
                with_hint(with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_ATTACK), Kind::Decimal,
                                                           {.kind = deluge::scenes::bind::Target::Kind::EFFECT,
                                                            .path = "audioCompressor/attack"}),
                                              2),
                          Hint::Attack),
                sid(deluge::l10n::String::STRING_FOR_ATTACK_SHORT)),
            HorizontalStep{.coarse = 1, .fine = 1}),
        compressor_banner<CompressorShown::Attack>),
    compressor_screen<CompressorShown::Attack>);
inline constexpr Node comp_release = with_leaf_text(
    with_value_text(
        with_horizontal_step(
            with_column_name(
                with_hint(with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_RELEASE), Kind::Decimal,
                                                           {.kind = deluge::scenes::bind::Target::Kind::EFFECT,
                                                            .path = "audioCompressor/release"}),
                                              1),
                          Hint::Release),
                sid(deluge::l10n::String::STRING_FOR_RELEASE_SHORT)),
            HorizontalStep{.coarse = 1, .fine = 1}),
        compressor_banner<CompressorShown::Release>),
    compressor_screen<CompressorShown::Release>);
inline constexpr Node comp_hpf = with_leaf_text(
    with_value_text(
        with_horizontal_step(
            with_hint(with_decimal_places(setting_item(sid(deluge::l10n::String::STRING_FOR_HPF), Kind::Decimal,
                                                       {.kind = deluge::scenes::bind::Target::Kind::EFFECT,
                                                        .path = "audioCompressor/compHPF"}),
                                          2),
                      Hint::Hpf),
            HorizontalStep{.coarse = 1, .fine = 1}),
        compressor_banner<CompressorShown::SidechainHpf>),
    compressor_screen<CompressorShown::SidechainHpf>);

inline constexpr const Node* kCompChildren[] = {&comp_threshold, &comp_ratio,   &comp_blend,
                                                &comp_attack,    &comp_release, &comp_hpf};
inline constexpr Node audio_comp = submenu(sid(deluge::l10n::String::STRING_FOR_COMMUNITY_FEATURE_MASTER_COMPRESSOR),
                                           kCompChildren, Layout::Horizontal);

/// @brief This file's contribution to `kBatch1Roots`: `audioCompMenu` (`menus.cpp`).
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundCompressorChildren;

} // namespace deluge::scenes::menu::nodes
