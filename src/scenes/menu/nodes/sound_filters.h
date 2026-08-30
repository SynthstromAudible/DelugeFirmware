#pragma once

/// @brief The filters subtree as data: `sound_filters` -> {`lpf`, `hpf`, `filter_routing`}.
///
/// Every node is `inline constexpr`, so it has one address across every translation unit that
/// includes this header (fix round 1, I3). That is what lets Task 9's shortcut tables be
/// `constexpr const Node* shortcuts_sound[16][8]` naming `&lpf_freq` directly, the way legacy's
/// `paramShortcutsForSounds` names `&lpfFreqMenu` -- rather than walking `children` spans at
/// runtime. The `.cpp` keeps the `get`/`set` hook bodies and the exported `k…Children` span.
///
/// Transcribed from the retired generator's declarative spec
/// (`gui/menu_item/generate/dmenus/filter/{lpf,hpf,__init__}.py`) and its committed output
/// (`generate/g_menus.inc:2-30`, at `ce4a79de0`), with the get/set logic of the two non-`ParamRef`
/// items taken from the legacy classes they instantiated:
///
// clang-format off
/// | node | legacy global (`g_menus.inc`) | legacy class |
/// |---|---|---|
/// | `lpf_mode` | `lpfModeMenu` (`:259`) | `filter::FilterModeSelection` (`filter/mode.h:34`) |
/// | `lpf_freq` | `lpfFreqMenu` (`:260`) | `filter::FilterParam` (`filter/param.h:33`) |
/// | `lpf_res` | `lpfResMenu` (`:261`) | `filter::FilterParam` |
/// | `lpf_morph` | `lpfMorphMenu` (`:262`) | `filter::FilterParam` |
/// | `lpf` | `lpfMenu` (`:269`) | `HorizontalMenu` |
/// | `hpf_mode` | `hpfModeMenu` (`:270`) | `filter::FilterModeSelection` |
/// | `hpf_freq` | `hpfFreqMenu` (`:271`) | `filter::FilterParam` |
/// | `hpf_res` | `hpfResMenu` (`:272`) | `filter::FilterParam` |
/// | `hpf_morph` | `hpfMorphMenu` (`:273`) | `filter::FilterParam` |
/// | `hpf` | `hpfMenu` (`:280`) | `HorizontalMenu` |
/// | `filter_routing` | `filterRoutingMenu` (`:281`) | `FilterRouting` (`filter_route.h:27`) |
/// | `sound_filters` | `soundFiltersMenu` (`:287`) | `Submenu` |
// clang-format on
///
/// @note **The morph items' name.** The Python spec gives `lpfMorphMenu` `name=STRING_FOR_MORPH,
///       title=STRING_FOR_LPF_MORPH`, but its emitted argument list is `["{title}", ...]`, so the
///       generated line passes the *title* into the two-argument constructor and the item's name and
///       title are both `STRING_FOR_LPF_MORPH` (`g_menus.inc:5`). `g_menus.inc` is what the firmware
///       ships, so that is what is transcribed. Legacy then overrides `getName()`/`getTitle()` to
///       return the *filter mode's* morph name instead (`filter/param.h:40-41` ->
///       `FilterInfo::getMorphNameOr`, `filter/info.h:78-89`) -- a dynamic name a `Node` has no
///       field for. Recorded as a known gap; E2's dynamic-label work is where it lands.
///
/// @note **`Node::hint` on the four `FilterParam`s is `Bar`, not `Knob`.** `FilterParam` inherits
///       `getRenderingStyle() == KNOB` (`gui/menu_item/number.h:48`) but overrides
///       `renderInHorizontalMenu` to call `drawBar` -- or `drawSlider`, when the item is the MORPH
///       one and the filter is an SVF (`filter/param.h:51-63`). The hint names what is drawn, which
///       is the fact the toolkit needs; the mode-dependent slider is dynamic and has no `Node`
///       spelling, so MORPH takes `Bar` too.
///
/// @note **`showColumnLabel()`/`showNotification()` have no `Node` spelling.** `FilterRouting`
///       returns false for both (`filter_route.h:63-64`); so do `unison::Count` (`count.h:66`),
///       `osc::Type` (`osc/type.h:127`) and `osc::AudioRecorder` (`audio_recorder.h:78-79`). Unlike
///       `getOccupiedSlots()` (which `Node::slots` carries), there is no field for them, so the four
///       overrides are dropped for now and recorded here and in the task-6 report's gap list.

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/nodes/patch_cables.h" // with_patch_chain -- every PATCHED param chains
#include "scenes/menu/relevance.h"

#include <span>

namespace deluge::scenes::menu {
struct MenuContext;
} // namespace deluge::scenes::menu

namespace deluge::scenes::menu::nodes {

/// @name The filter-mode and filter-route hooks. Bodies (and their legacy citations) in the `.cpp`.
/// @{
int32_t get_lpf_mode(const MenuContext& ctx);
void set_lpf_mode(const MenuContext& ctx, int32_t value);
int32_t get_hpf_mode(const MenuContext& ctx);
void set_hpf_mode(const MenuContext& ctx, int32_t value);
int32_t get_filter_route(const MenuContext& ctx);
void set_filter_route(const MenuContext& ctx, int32_t value);
/// @}

/// `FilterModeSelection::getOptions`'s LPF list (`gui/menu_item/filter/mode.h:73-79`).
inline constexpr uint16_t kLpfModeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_12DB_LADDER), sid(deluge::l10n::String::STRING_FOR_24DB_LADDER),
    sid(deluge::l10n::String::STRING_FOR_DRIVE),       sid(deluge::l10n::String::STRING_FOR_SVF_BAND),
    sid(deluge::l10n::String::STRING_FOR_SVF_NOTCH),   sid(deluge::l10n::String::STRING_FOR_OFF),
};

/// `FilterModeSelection::getOptions`'s HPF list (`filter/mode.h:65-71`).
inline constexpr uint16_t kHpfModeNames[] = {
    sid(deluge::l10n::String::STRING_FOR_SVF_BAND),
    sid(deluge::l10n::String::STRING_FOR_SVF_NOTCH),
    sid(deluge::l10n::String::STRING_FOR_HPLADDER),
    sid(deluge::l10n::String::STRING_FOR_OFF),
};

/// `FilterRouting::getOptions` (`gui/menu_item/filter_route.h:57-60`).
inline constexpr uint16_t kFilterRouteNames[] = {
    sid(deluge::l10n::String::STRING_FOR_HPF_TO_LPF),
    sid(deluge::l10n::String::STRING_FOR_LPF_TO_HPF),
    sid(deluge::l10n::String::STRING_FOR_PARALLEL),
};

inline constexpr Node lpf_mode =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_MODE), kLpfModeNames, get_lpf_mode, set_lpf_mode),
               sid(deluge::l10n::String::STRING_FOR_LPF_MODE));
inline constexpr Node lpf_freq =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FREQUENCY),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_FREQ},
                           Hint::Bar, relevance::has_lpf, sid(deluge::l10n::String::STRING_FOR_LPF_FREQUENCY)));
inline constexpr Node lpf_res =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_RESONANCE),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_RESONANCE},
                           Hint::Bar, relevance::has_lpf, sid(deluge::l10n::String::STRING_FOR_LPF_RESONANCE)));
inline constexpr Node lpf_morph =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_LPF_MORPH),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_LPF_MORPH},
                           Hint::Bar, relevance::has_lpf));
inline constexpr const Node* kLpfChildren[] = {&lpf_mode, &lpf_freq, &lpf_res, &lpf_morph};
inline constexpr Node lpf = submenu(sid(deluge::l10n::String::STRING_FOR_LPF), kLpfChildren, Layout::Horizontal);

inline constexpr Node hpf_mode =
    with_title(enumeration(sid(deluge::l10n::String::STRING_FOR_MODE), kHpfModeNames, get_hpf_mode, set_hpf_mode),
               sid(deluge::l10n::String::STRING_FOR_HPF_MODE));
inline constexpr Node hpf_freq =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_FREQUENCY),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_HPF_FREQ},
                           Hint::Bar, relevance::has_hpf, sid(deluge::l10n::String::STRING_FOR_HPF_FREQUENCY)));
inline constexpr Node hpf_res =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_RESONANCE),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_HPF_RESONANCE},
                           Hint::Bar, relevance::has_hpf, sid(deluge::l10n::String::STRING_FOR_HPF_RESONANCE)));
inline constexpr Node hpf_morph =
    with_patch_chain(param(sid(deluge::l10n::String::STRING_FOR_HPF_MORPH),
                           {deluge::modulation::params::Kind::PATCHED, deluge::modulation::params::LOCAL_HPF_MORPH},
                           Hint::Bar, relevance::has_hpf));
inline constexpr const Node* kHpfChildren[] = {&hpf_mode, &hpf_freq, &hpf_res, &hpf_morph};
inline constexpr Node hpf = submenu(sid(deluge::l10n::String::STRING_FOR_HPF), kHpfChildren, Layout::Horizontal);

/// Four slots wide -- `FilterRouting::getOccupiedSlots()` (`filter_route.h:62`), which draws the
/// whole page rather than one column.
inline constexpr Node filter_routing = with_slots(enumeration(sid(deluge::l10n::String::STRING_FOR_FILTER_ROUTE),
                                                              kFilterRouteNames, get_filter_route, set_filter_route),
                                                  4);

inline constexpr const Node* kFiltersChildren[] = {&lpf, &hpf, &filter_routing};
inline constexpr Node sound_filters = submenu(sid(deluge::l10n::String::STRING_FOR_FILTERS), kFiltersChildren);

/// @brief This file's contribution to `kBatch1Roots`: `soundFiltersMenu` (`g_menus.inc:30`).
///
/// Defined in the matching `.cpp`, which is also where the `get`/`set` hook bodies live.
extern const std::span<const Node* const> kSoundFiltersChildren;

} // namespace deluge::scenes::menu::nodes
