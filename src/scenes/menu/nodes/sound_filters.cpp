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
/// @brief The filters subtree: `soundFiltersMenu` -> {`lpfMenu`, `hpfMenu`, `filterRoutingMenu`}.
///
/// Transcribed from the retired generator's declarative spec
/// (`gui/menu_item/generate/dmenus/filter/{lpf,hpf,__init__}.py`) and its committed output
/// (`generate/g_menus.inc:2-30`), with the get/set logic of the two non-`ParamRef` items taken from
/// the legacy classes they instantiated:
///
/// | node | legacy global (`g_menus.inc`) | legacy class |
/// |---|---|---|
/// | `lpf_mode` | `lpfModeMenu` (`:2`) | `filter::FilterModeSelection` (`filter/mode.h:18`) |
/// | `lpf_freq` | `lpfFreqMenu` (`:3`) | `filter::FilterParam` (`filter/param.h:17`) |
/// | `lpf_res` | `lpfResMenu` (`:4`) | `filter::FilterParam` |
/// | `lpf_morph` | `lpfMorphMenu` (`:5`) | `filter::FilterParam` |
/// | `lpf` | `lpfMenu` (`:12`) | `HorizontalMenu` |
/// | `hpf_mode` | `hpfModeMenu` (`:13`) | `filter::FilterModeSelection` |
/// | `hpf_freq` | `hpfFreqMenu` (`:14`) | `filter::FilterParam` |
/// | `hpf_res` | `hpfResMenu` (`:15`) | `filter::FilterParam` |
/// | `hpf_morph` | `hpfMorphMenu` (`:16`) | `filter::FilterParam` |
/// | `hpf` | `hpfMenu` (`:23`) | `HorizontalMenu` |
/// | `filter_routing` | `filterRoutingMenu` (`:24`) | `FilterRouting` (`filter_route.h:27`) |
/// | `sound_filters` | `soundFiltersMenu` (`:30`) | `Submenu` |
///
/// @note **The morph items' name.** The Python spec gives `lpfMorphMenu` `name=STRING_FOR_MORPH,
///       title=STRING_FOR_LPF_MORPH`, but its emitted argument list is `["{title}", ...]`, so the
///       generated line passes the *title* into the two-argument constructor and the item's name and
///       title are both `STRING_FOR_LPF_MORPH` (`g_menus.inc:5`). `g_menus.inc` is what the firmware
///       ships, so that is what is transcribed. Legacy then overrides `getName()`/`getTitle()` to
///       return the *filter mode's* morph name instead (`filter/param.h:38-39` ->
///       `FilterInfo::getMorphNameOr`, `filter/info.h:62-69`) -- a dynamic name a `Node` has no
///       field for. Recorded as a known gap; E2's dynamic-label work is where it lands.
///
/// @note **`Node::hint` on the four `FilterParam`s is `Bar`, not `Knob`.** `FilterParam` inherits
///       `getRenderingStyle() == KNOB` (`gui/menu_item/number.h:48`) but overrides
///       `renderInHorizontalMenu` to call `drawBar` -- or `drawSlider`, when the item is the MORPH
///       one and the filter is an SVF (`filter/param.h:51-63`). The hint names what is drawn, which
///       is the fact the toolkit needs; the mode-dependent slider is dynamic and has no `Node`
///       spelling, so MORPH takes `Bar` too.

#include "scenes/menu/nodes/nodes.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/detail.h"
#include "scenes/menu/relevance.h"

#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"

#include <algorithm>
#include <array>

namespace deluge::scenes::menu::nodes {

namespace {

using deluge::l10n::String;
namespace params = deluge::modulation::params;

// ---------------------------------------------------------------------------------------------
// Filter mode -- `filter::FilterModeSelection` (`gui/menu_item/filter/mode.h:18-69`) over
// `FilterInfo` (`filter/info.h:34-58`).

/// `FilterInfo::getModeValue()`'s LPF arm (`filter/info.h:39-42`): "Off is located past the
/// HPFLadder, which isn't an option for the low pass filter".
int32_t get_lpf_mode(const MenuContext& ctx) {
	if (ctx.mod == nullptr) {
		return 0;
	}
	return std::min(static_cast<int32_t>(ctx.mod->lpfMode), kNumLPFModes);
}

/// `FilterInfo::setModeForModControllable`'s LPF arm (`filter/info.h:50-57`), applied through the
/// affect-entire loop `FilterModeSelection::writeCurrentValue` spells out (`filter/mode.h:44-59`).
void set_lpf_mode(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    t.mod->lpfMode = v >= kNumLPFModes ? ::FilterMode::OFF : static_cast<::FilterMode>(v);
	    });
}

/// `FilterInfo::getModeValue()`'s HPF arm (`filter/info.h:35-37`).
int32_t get_hpf_mode(const MenuContext& ctx) {
	if (ctx.mod == nullptr) {
		return 0;
	}
	return static_cast<int32_t>(ctx.mod->hpfMode) - kFirstHPFMode;
}

/// `FilterInfo::setModeForModControllable`'s HPF arm (`filter/info.h:46-48`).
void set_hpf_mode(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(ctx, value, 0,
	                           [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		                           t.mod->hpfMode = static_cast<::FilterMode>(v + kFirstHPFMode);
	                           });
}

/// `FilterModeSelection::getOptions`'s LPF list (`filter/mode.h:57-64`).
constexpr uint16_t kLpfModeNames[] = {
    sid(String::STRING_FOR_12DB_LADDER), sid(String::STRING_FOR_24DB_LADDER), sid(String::STRING_FOR_DRIVE),
    sid(String::STRING_FOR_SVF_BAND),    sid(String::STRING_FOR_SVF_NOTCH),   sid(String::STRING_FOR_OFF),
};

/// `FilterModeSelection::getOptions`'s HPF list (`filter/mode.h:50-55`).
constexpr uint16_t kHpfModeNames[] = {
    sid(String::STRING_FOR_SVF_BAND),
    sid(String::STRING_FOR_SVF_NOTCH),
    sid(String::STRING_FOR_HPLADDER),
    sid(String::STRING_FOR_OFF),
};

// ---------------------------------------------------------------------------------------------
// Filter routing -- `FilterRouting` (`gui/menu_item/filter_route.h:27-70`).

int32_t get_filter_route(const MenuContext& ctx) {
	return ctx.mod != nullptr ? static_cast<int32_t>(ctx.mod->filterRoute) : 0; // `filter_route.h:30`
}

void set_filter_route(const MenuContext& ctx, int32_t value) {
	bindings::apply_to_targets(
	    ctx, value, 0, [](const bindings::SoundTarget& t, int32_t v, int32_t, const MenuContext&) {
		    // `filter_route.h:45`: the kit loop -- and only the kit loop -- skips a drum whose filters
		    // are not both on. The single-target path (`:53`) has no such test.
		    if (t.whole_kit && (t.mod->lpfMode == ::FilterMode::OFF || t.mod->hpfMode == ::FilterMode::OFF)) {
			    return;
		    }
		    t.mod->filterRoute = static_cast<::FilterRoute>(v);
	    });
}

/// `FilterRouting::getOptions` (`filter_route.h:57-60`).
constexpr uint16_t kFilterRouteNames[] = {
    sid(String::STRING_FOR_HPF_TO_LPF),
    sid(String::STRING_FOR_LPF_TO_HPF),
    sid(String::STRING_FOR_PARALLEL),
};

// ---------------------------------------------------------------------------------------------
// The tree.

constexpr Node lpf_mode =
    with_title(enumeration(sid(String::STRING_FOR_MODE), kLpfModeNames, get_lpf_mode, set_lpf_mode),
               sid(String::STRING_FOR_LPF_MODE));
constexpr Node lpf_freq = param(sid(String::STRING_FOR_FREQUENCY), {params::Kind::PATCHED, params::LOCAL_LPF_FREQ},
                                Hint::Bar, relevance::has_lpf, sid(String::STRING_FOR_LPF_FREQUENCY));
constexpr Node lpf_res = param(sid(String::STRING_FOR_RESONANCE), {params::Kind::PATCHED, params::LOCAL_LPF_RESONANCE},
                               Hint::Bar, relevance::has_lpf, sid(String::STRING_FOR_LPF_RESONANCE));
constexpr Node lpf_morph = param(sid(String::STRING_FOR_LPF_MORPH), {params::Kind::PATCHED, params::LOCAL_LPF_MORPH},
                                 Hint::Bar, relevance::has_lpf);
constexpr const Node* kLpfChildren[] = {&lpf_mode, &lpf_freq, &lpf_res, &lpf_morph};
constexpr Node lpf = submenu(sid(String::STRING_FOR_LPF), kLpfChildren, Layout::Horizontal);

constexpr Node hpf_mode =
    with_title(enumeration(sid(String::STRING_FOR_MODE), kHpfModeNames, get_hpf_mode, set_hpf_mode),
               sid(String::STRING_FOR_HPF_MODE));
constexpr Node hpf_freq = param(sid(String::STRING_FOR_FREQUENCY), {params::Kind::PATCHED, params::LOCAL_HPF_FREQ},
                                Hint::Bar, relevance::has_hpf, sid(String::STRING_FOR_HPF_FREQUENCY));
constexpr Node hpf_res = param(sid(String::STRING_FOR_RESONANCE), {params::Kind::PATCHED, params::LOCAL_HPF_RESONANCE},
                               Hint::Bar, relevance::has_hpf, sid(String::STRING_FOR_HPF_RESONANCE));
constexpr Node hpf_morph = param(sid(String::STRING_FOR_HPF_MORPH), {params::Kind::PATCHED, params::LOCAL_HPF_MORPH},
                                 Hint::Bar, relevance::has_hpf);
constexpr const Node* kHpfChildren[] = {&hpf_mode, &hpf_freq, &hpf_res, &hpf_morph};
constexpr Node hpf = submenu(sid(String::STRING_FOR_HPF), kHpfChildren, Layout::Horizontal);

/// Four slots wide -- `FilterRouting::getOccupiedSlots()` (`filter_route.h:62`), which draws the
/// whole page rather than one column.
constexpr Node filter_routing = with_slots(
    enumeration(sid(String::STRING_FOR_FILTER_ROUTE), kFilterRouteNames, get_filter_route, set_filter_route), 4);

constexpr const Node* kFiltersChildren[] = {&lpf, &hpf, &filter_routing};
constexpr Node sound_filters = submenu(sid(String::STRING_FOR_FILTERS), kFiltersChildren);

constexpr const Node* kRoots[] = {&sound_filters};

} // namespace

const std::span<const Node* const> kSoundFiltersChildren{kRoots};

} // namespace deluge::scenes::menu::nodes
