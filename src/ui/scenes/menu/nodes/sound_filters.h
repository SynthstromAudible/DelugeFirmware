#pragma once

/// @brief The filters subtree as data: `sound_filters` -> {`lpf`, `hpf`, `filter_routing`}.
///
/// Every node is `inline constexpr`, so it has one address across every translation unit that
/// includes this header. That is what lets the shortcut tables be
/// `constexpr const Node* shortcuts_sound[16][8]` naming `&lpf_freq` directly, the way legacy's
/// `paramShortcutsForSounds` names `&lpfFreqMenu` -- rather than walking `children` spans at
/// runtime. The `.cpp` keeps the `get`/`set` hook bodies and the exported `k…Children` span.
///
/// Transcribed from `origin/main:src/deluge/gui/ui/menus.cpp`'s filter block, from `lpfModeMenu`, with the get/set
/// logic of the two non-`ParamRef` items taken from the legacy classes they instantiated:
///
// clang-format off
/// | node | legacy global | legacy class |
/// |---|---|---|
/// | `lpf_mode` | `lpfModeMenu` | `filter::FilterModeSelection` (`origin/main:src/deluge/gui/menu_item/filter/mode.h`) |
/// | `lpf_freq` | `lpfFreqMenu` | `filter::FilterParam` (`origin/main:src/deluge/gui/menu_item/filter/param.h`) |
/// | `lpf_res` | `lpfResMenu` | `filter::FilterParam` |
/// | `lpf_morph` | `lpfMorphMenu` | `filter::FilterParam` |
/// | `lpf` | `lpfMenu` | `HorizontalMenu` |
/// | `hpf_mode` | `hpfModeMenu` | `filter::FilterModeSelection` |
/// | `hpf_freq` | `hpfFreqMenu` | `filter::FilterParam` |
/// | `hpf_res` | `hpfResMenu` | `filter::FilterParam` |
/// | `hpf_morph` | `hpfMorphMenu` | `filter::FilterParam` |
/// | `hpf` | `hpfMenu` | `HorizontalMenu` |
/// | `filter_routing` | `filterRoutingMenu` | `FilterRouting` |
/// | `sound_filters` | `soundFiltersMenu` | `Submenu` |
// clang-format on
///
/// @note **The morph items' name.** `lpfMorphMenu` in `origin/main:src/deluge/gui/ui/menus.cpp` passes
///       `STRING_FOR_LPF_MORPH` as both the item's name and its title (not `STRING_FOR_MORPH` for the name), and that
///       file is what the firmware ships, so that is what is transcribed. Legacy then overrides
///       `getName()`/`getTitle()` to return the *filter mode's* morph name instead
///       (`origin/main:src/deluge/gui/menu_item/filter/param.h` -> `FilterInfo::getMorphNameOr`,
///       `origin/main:src/deluge/gui/menu_item/filter/info.h`), and its column label to the short morph name
///       (`FilterParam::getColumnLabel` in `origin/main:src/deluge/gui/menu_item/filter/param.h`). Neither is carried
///       yet: the morph columns show the static name.
///
/// @note **`Node::hint` on the four `FilterParam`s is `Bar`, not `Knob`.** `FilterParam` inherits
///       `getRenderingStyle() == KNOB` (`origin/main:src/deluge/gui/menu_item/number.h`) but overrides
///       `renderInHorizontalMenu` to call `drawBar` -- or `drawSlider`, when the item is the MORPH one and the filter
///       is an SVF (`origin/main:src/deluge/gui/menu_item/filter/param.h`). The hint names what is drawn, which is the
///       fact the toolkit needs; the mode-dependent slider is dynamic and has no `Node` spelling, so MORPH takes `Bar`
///       too.
///
/// @note **`FilterRouting` draws no label and raises no notification**: its `showColumnLabel()` and
///       `showNotification()` are false (`origin/main:src/deluge/gui/menu_item/filter_route.h`), so `filter_routing` is
///       `without_column_label` and `without_notification`.

#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/nodes/column_hooks.h"
#include "ui/scenes/menu/nodes/detail.h"
#include "ui/scenes/menu/nodes/fan_out_rules.h"
#include "ui/scenes/menu/nodes/param_shows.h"
#include "ui/scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains
#include "ui/scenes/menu/relevance.h"

#include <span>

namespace deluge::ui::scenes::menu {
struct MenuContext;
} // namespace deluge::ui::scenes::menu

namespace deluge::ui::scenes::menu::nodes {

inline constexpr Node lpf_mode = with_affect_entire(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_MODE), Kind::Enum,
                            {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "lpfMode"}),
               sid(deluge::l10n::String::STRING_FOR_LPF_MODE)));
inline constexpr Node lpf_freq =
    with_column_draw(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FREQUENCY),
                                            sound_param(deluge::instruments::classic::param::kLpfFrequency), Hint::Bar,
                                            param_shows::kHasLpf, sid(deluge::l10n::String::STRING_FOR_LPF_FREQUENCY))),
                     lpf_graph_draw);
inline constexpr Node lpf_res = with_patch_chain(param(
    sid(deluge::l10n::String::STRING_FOR_RESONANCE), sound_param(deluge::instruments::classic::param::kLpfResonance),
    Hint::Bar, param_shows::kHasLpf, sid(deluge::l10n::String::STRING_FOR_LPF_RESONANCE)));
inline constexpr Node lpf_morph = with_name_text(
    with_value_text(with_column_draw(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_LPF_MORPH),
                                                            sound_param(deluge::instruments::classic::param::kLpfMorph),
                                                            Hint::Bar, param_shows::kHasLpf)),
                                     lpf_morph_draw),
                    lpf_morph_text),
    lpf_morph_text);
inline constexpr const Node* kLpfChildren[] = {&lpf_mode, &lpf_freq, &lpf_res, &lpf_morph};
inline constexpr Node lpf = submenu(sid(deluge::l10n::String::STRING_FOR_LPF), kLpfChildren, Layout::Horizontal);

inline constexpr Node hpf_mode = with_affect_entire(
    with_title(setting_item(sid(deluge::l10n::String::STRING_FOR_MODE), Kind::Enum,
                            {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT, .path = "hpfMode"}),
               sid(deluge::l10n::String::STRING_FOR_HPF_MODE)));
inline constexpr Node hpf_freq =
    with_column_draw(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FREQUENCY),
                                            sound_param(deluge::instruments::classic::param::kHpfFrequency), Hint::Bar,
                                            param_shows::kHasHpf, sid(deluge::l10n::String::STRING_FOR_HPF_FREQUENCY))),
                     hpf_graph_draw);
inline constexpr Node hpf_res = with_patch_chain(param(
    sid(deluge::l10n::String::STRING_FOR_RESONANCE), sound_param(deluge::instruments::classic::param::kHpfResonance),
    Hint::Bar, param_shows::kHasHpf, sid(deluge::l10n::String::STRING_FOR_HPF_RESONANCE)));
inline constexpr Node hpf_morph = with_name_text(
    with_reverse_in_horizontal(
        with_value_text(
            with_column_draw(with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_HPF_MORPH),
                                                    sound_param(deluge::instruments::classic::param::kHpfMorph),
                                                    Hint::Bar, param_shows::kHasHpf)),
                             hpf_morph_draw),
            hpf_morph_text),
        hpf_morph_reversed),
    hpf_morph_text);
inline constexpr const Node* kHpfChildren[] = {&hpf_mode, &hpf_freq, &hpf_res, &hpf_morph};
inline constexpr Node hpf = submenu(sid(deluge::l10n::String::STRING_FOR_HPF), kHpfChildren, Layout::Horizontal);

/// Four slots wide -- `FilterRouting::getOccupiedSlots()`, which draws the
/// whole page rather than one column. Affect-entire routes only the drums with both filters on
/// (`FilterRouting::writeCurrentValue` in `origin/main:src/deluge/gui/menu_item/filter_route.h`).
inline constexpr Node filter_routing = with_affect_entire(with_option_list_column(without_notification(
    without_column_label(with_slots(setting_item(sid(deluge::l10n::String::STRING_FOR_FILTER_ROUTE), Kind::Enum,
                                                 {.kind = deluge::ui::scenes::bind::Target::Kind::EFFECT,
                                                  .path = "filterRoute",
                                                  .fan_out_when = fan_out::kBothFilters}),
                                    4)))));

inline constexpr const Node* kFiltersChildren[] = {&lpf, &hpf, &filter_routing};
inline constexpr Node sound_filters = submenu(sid(deluge::l10n::String::STRING_FOR_FILTERS), kFiltersChildren);

/// @brief This file's contribution to `kBatch1Roots`: `soundFiltersMenu` (`origin/main:src/deluge/gui/ui/menus.cpp`).
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundFiltersChildren;

} // namespace deluge::ui::scenes::menu::nodes
