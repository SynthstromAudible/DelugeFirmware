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
/// @brief The patch-cable items' members, the four page nodes, and the five chain hooks. See
///        `patch_cables.h` for the map from legacy globals and for what did not come across.

#include "scenes/menu/nodes/patch_cables.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"

#include "gfx/oled/icons.h"
#include "ui/oled/list.h"
#include "ui/oled/routing.h"
#include "ui/oled/text.h"
#include "ui/oled/values.h"

#include "definitions_cxx.hpp"
#include "modulation/params/param.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h" // the sidechain volume item's reverb flag
#include "processing/sound/sound.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace params = deluge::modulation::params;
using deluge::l10n::String;

namespace {

// ---------------------------------------------------------------------------------------------
// Text helpers. `util/` is outside this layer's include allowance (`scripts/check_layer_rules.py`'s
// `scenes-includes-outside`), so the two `util/functions.cpp` name tables these pages need are
// transcribed here, as `sound_compressor.cpp` and `sound_sidechain.cpp` transcribe theirs.

/// @brief `sourceToStringShort` (`util/functions.cpp:381-431`) -- "all should be four chars, to fit
///        a fixed column layout", which is what the mod-matrix list's columns assume.
///
/// @note Three of the sixteen arms shipped wrong from E2 task 9 and were corrected by E4 task 3:
///       `PatchSource::X` said "modx" for legacy's "mpeX", `PatchSource::Y` said "mody" for
///       "mpeY", and the default said "none" for "----". All three are displayed text, so all
///       three were user-visible divergences. `menu_patch_cables_64.pbm` records the `Y` row and
///       moved with the fix. The sibling transcription in
///       `scenes/overlay/param_popup_scene.cpp` always had all sixteen right; the two are checked
///       against each other as well as against legacy.
std::string_view source_short_name(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL_1:
		return "lfo1";
	case PatchSource::LFO_GLOBAL_2:
		return "lfo3";
	case PatchSource::LFO_LOCAL_1:
		return "lfo2";
	case PatchSource::LFO_LOCAL_2:
		return "lfo4";
	case PatchSource::ENVELOPE_0:
		return "env1";
	case PatchSource::ENVELOPE_1:
		return "env2";
	case PatchSource::ENVELOPE_2:
		return "env3";
	case PatchSource::ENVELOPE_3:
		return "env4";
	case PatchSource::VELOCITY:
		return "velo";
	case PatchSource::NOTE:
		return "note";
	case PatchSource::SIDECHAIN:
		return "side";
	case PatchSource::RANDOM:
		return "rand";
	case PatchSource::AFTERTOUCH:
		return "pres";
	case PatchSource::X:
		return "mpeX";
	case PatchSource::Y:
		return "mpeY";
	default:
		return "----";
	}
}

/// @brief `getSourceDisplayName` (`util/functions.cpp:315-368`), as the l10n *ids* it returns
///        strings for -- resolved through the profile, this layer's one sanctioned text path.
uint16_t source_display_string(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL_1:
		return sid(String::STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_1);
	case PatchSource::LFO_GLOBAL_2:
		return sid(String::STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_2);
	case PatchSource::LFO_LOCAL_1:
		return sid(String::STRING_FOR_PATCH_SOURCE_LFO_LOCAL_1);
	case PatchSource::LFO_LOCAL_2:
		return sid(String::STRING_FOR_PATCH_SOURCE_LFO_LOCAL_2);
	case PatchSource::ENVELOPE_0:
		return sid(String::STRING_FOR_PATCH_SOURCE_ENVELOPE_0);
	case PatchSource::ENVELOPE_1:
		return sid(String::STRING_FOR_PATCH_SOURCE_ENVELOPE_1);
	case PatchSource::ENVELOPE_2:
		return sid(String::STRING_FOR_PATCH_SOURCE_ENVELOPE_2);
	case PatchSource::ENVELOPE_3:
		return sid(String::STRING_FOR_PATCH_SOURCE_ENVELOPE_3);
	case PatchSource::VELOCITY:
		return sid(String::STRING_FOR_PATCH_SOURCE_VELOCITY);
	case PatchSource::NOTE:
		return sid(String::STRING_FOR_PATCH_SOURCE_NOTE);
	case PatchSource::SIDECHAIN:
		return sid(String::STRING_FOR_PATCH_SOURCE_SIDECHAIN);
	case PatchSource::RANDOM:
		return sid(String::STRING_FOR_PATCH_SOURCE_RANDOM);
	case PatchSource::AFTERTOUCH:
		return sid(String::STRING_FOR_PATCH_SOURCE_AFTERTOUCH);
	case PatchSource::X:
		return sid(String::STRING_FOR_PATCH_SOURCE_X);
	case PatchSource::Y:
		return sid(String::STRING_FOR_PATCH_SOURCE_Y);
	default:
		return sid(String::STRING_FOR_PATCH_SOURCE_UNKNOWN);
	}
}

/// @brief Append @p s to @p out, up to @p cap.
void append(char* out, size_t& len, size_t cap, std::string_view s) {
	for (const char c : s) {
		if (len + 1 < cap) {
			out[len++] = c;
		}
	}
}

/// @brief A menu-unit cable value (-5000..5000) as "-12.34" -- the `floatToString(level / 100, ...,
///        2, 2)` at `patch_cables.cpp:80`, without the float.
void append_cable_value(char* out, size_t& len, size_t cap, int32_t value) {
	if (value < 0) {
		append(out, len, cap, "-");
		value = -value;
	}
	char digits[8];
	size_t d = 0;
	int32_t whole = value / 100;
	do {
		digits[d++] = static_cast<char>('0' + whole % 10);
		whole /= 10;
	} while (whole != 0 && d < sizeof(digits));
	while (d > 0) {
		if (len + 1 < cap) {
			out[len++] = digits[--d];
		}
		else {
			--d;
		}
	}
	const int32_t frac = value % 100;
	if (len + 1 < cap) {
		out[len++] = '.';
	}
	if (len + 1 < cap) {
		out[len++] = static_cast<char>('0' + frac / 10);
	}
	if (len + 1 < cap) {
		out[len++] = static_cast<char>('0' + frac % 10);
	}
}

// ---------------------------------------------------------------------------------------------
// Model access.

/// @brief `soundEditor.currentParamManager->getPatchCableSet()`, or null when there is no param
///        manager (a settings-root context, say) or no patch-cable collection in it.
///
/// **`getPatchCableSetAllowJibberish()`, not `getPatchCableSet()`.** The latter
/// `FREEZE_WITH_ERROR("E412")`s on a param manager whose third summary holds no collection
/// (`modulation/params/param_manager.h:155-162`, under `ALPHA_OR_BETA_VERSION`), and
/// `freezeWithError` dereferences the `display` global -- which is null in the host build, so the
/// assertion is a segfault rather than a diagnostic. Legacy never hit it because `SoundEditor` only
/// ever put a patched sound's param manager in `currentParamManager`; this layer's contexts include
/// several that carry an unpatched one, so the unchecked read plus a null test is the right call.
PatchCableSet* cable_set(const MenuContext& ctx) {
	return ctx.params != nullptr ? ctx.params->getPatchCableSetAllowJibberish() : nullptr;
}

/// @brief `((int64_t)paramValue * kMaxMenuPatchCableValue + (1 << 29)) >> 30` -- the stored-to-menu
///        rescale `PatchCableStrength::readCurrentValue` and `PatchCables::renderOptions` share
///        (`patch_cable_strength.cpp:200-204`, `patch_cables.cpp:74-75`).
int32_t cable_value_to_menu(int32_t param_value) {
	return static_cast<int32_t>((static_cast<int64_t>(param_value) * kMaxMenuPatchCableValue + (1 << 29)) >> 30);
}

/// @brief `SourceSelection`'s own option order (`source_selection.cpp:31-47`) -- NOT the `PatchSource`
///        enumerator order, which is why it is a table rather than a loop over the enum.
constexpr std::array<PatchSource, kNumPatchSources> kSourceMenuContents{
    PatchSource::ENVELOPE_0,
    PatchSource::ENVELOPE_1,
    PatchSource::ENVELOPE_2,
    PatchSource::ENVELOPE_3,
    PatchSource::LFO_GLOBAL_1,
    PatchSource::LFO_LOCAL_1,
    PatchSource::LFO_GLOBAL_2,
    PatchSource::LFO_LOCAL_2,
    PatchSource::VELOCITY,
    PatchSource::NOTE,
    PatchSource::SIDECHAIN,
    PatchSource::RANDOM,
    PatchSource::X,
    PatchSource::Y,
    PatchSource::AFTERTOUCH,
};

/// @brief The destination descriptor a source picker or strength page addresses.
///
/// `Regular` addresses the bare param (`source_selection/regular.cpp:26-30`,
/// `patch_cable_strength/regular.cpp:36-40`); `Range` addresses that param *plus* the regular
/// source, i.e. the cable whose depth is being modulated (`source_selection/range.cpp:26-30`,
/// `patch_cable_strength/range.cpp:32-36`).
ParamDescriptor destination_for(const MenuState& state, bool range) {
	ParamDescriptor desc{};
	if (range) {
		desc.setToHaveParamAndSource(state.patching_param, state.patch_source);
	}
	else {
		desc.setToHaveParamOnly(state.patching_param);
	}
	return desc;
}

/// @brief `SourceSelection::sourceIsAllowed` (`source_selection.cpp:178-216`).
bool source_is_allowed(const MenuContext& ctx, PatchSource source, ParamDescriptor destination) {
	// `:181-192` -- patching to another cable's range: a global source can control any range, a
	// local one only a cable that goes to a local param.
	if (!destination.isJustAParam()) {
		if (source < kFirstLocalSource) {
			return true;
		}
		return destination.getJustTheParam() < params::FIRST_GLOBAL;
	}
	if (ctx.sound == nullptr || ctx.params == nullptr) {
		return false;
	}
	auto* param_manager = static_cast<ParamManagerForTimeline*>(ctx.params);
	const int32_t p = destination.getJustTheParam();
	const auto allowed = [&](int32_t target) {
		return ctx.sound->maySourcePatchToParam(source, static_cast<uint8_t>(target), param_manager)
		       != PatchCableAcceptance::DISALLOWED;
	};
	// `:196-215` -- the volume triple is one destination as far as the user is concerned, so any of
	// the three accepting the source makes it offerable.
	if (p == params::GLOBAL_VOLUME_POST_FX) {
		return allowed(params::GLOBAL_VOLUME_POST_FX) || allowed(params::LOCAL_VOLUME)
		       || allowed(params::GLOBAL_VOLUME_POST_REVERB_SEND);
	}
	return allowed(p);
}

/// @brief `PatchCableSet::isSourcePatchedToDestinationDescriptorVolumeInspecific` -- legacy's
///        `shouldDrawDotOnValue` (`source_selection.cpp:52-57`).
bool source_is_patched(const MenuContext& ctx, PatchSource source, ParamDescriptor destination) {
	PatchCableSet* set = cable_set(ctx);
	return set != nullptr && set->isSourcePatchedToDestinationDescriptorVolumeInspecific(source, destination);
}

/// @brief The frame-local rect the two list pages draw their rows into, and the node whose title
///        they draw -- the same three lines `midi_command.cpp`'s `draw_title_for` owns.
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

void draw_title_for(deluge::ui::oled::OledFrame& f, const MenuState& state) {
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});
}

/// @brief The per-depth widget state a list page borrows (`MenuState::list` / `::scroll`).
size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// PatchCablesItem -- `menu_item::PatchCables`.

void PatchCablesItem::on_focus(const MenuContext& /*ctx*/, MenuState& state) const {
	// `PatchCables::beginSession` (`patch_cables.cpp:16-26`): the cursor starts at 0 on a forward
	// entry and is restored from `savedVal` on the way back. The port needs no `savedVal`: coming
	// *back* from a chained page runs `MenuScene::ascend`, which does not call `on_focus` at all, so
	// `list[depth]` still holds the cursor the user left. This hook is therefore the forward-entry
	// arm alone, and `on_focus_backward` (E3 task 10's port of the `savedVal` restore) is left on
	// its do-nothing default -- deliberately, not by omission: `list[depth]` already holding the
	// value `savedVal` would restore is exactly what makes the default correct here, unlike
	// `GateSelectionItem`'s (`nodes/dynamic.cpp`), which needs the hook because its own cursor is
	// not what legacy restores.
	state.list[depth_of(state)] = deluge::ui::ListState{};
	state.scroll[depth_of(state)] = deluge::ui::ScrollText{};
}

void PatchCablesItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	// `PatchCables::renderOptions` (`patch_cables.cpp:42-89`). Legacy keeps its row buffers in
	// SDRAM BSS because its `options` vector holds views into them across frames; this port rebuilds
	// them every frame in the render frame's own stack, which is what an immediate-mode widget wants
	// and costs 32 x 32 bytes.
	constexpr size_t kRowChars = 32;
	static_assert(kMaxNumPatchCables <= 64, "the row buffer below is sized for legacy's 32 cables");
	char bufs[kMaxNumPatchCables][kRowChars];
	deluge::ui::oled::ListRow rows[kMaxNumPatchCables];
	size_t n = 0;

	PatchCableSet* set = cable_set(ctx);
	const int32_t count = set != nullptr ? set->numPatchCables : 0;
	for (int32_t i = 0; i < count && n < kMaxNumPatchCables; i++) {
		PatchCable& cable = set->patchCables[i];
		const ParamDescriptor desc = cable.destinationParamDescriptor;
		size_t len = 0;
		char* buf = bufs[n];
		append(buf, len, kRowChars, source_short_name(cable.from)); // `:60-62`
		append(buf, len, kRowChars, " ");
		if (!desc.isJustAParam()) { // `:67-72` -- a cable modulating another cable's depth
			append(buf, len, kRowChars, source_short_name(desc.getTopLevelSource()));
			append(buf, len, kRowChars, " ");
		}
		append_cable_value(buf, len, kRowChars, cable_value_to_menu(cable.param.getCurrentValue())); // `:74-80`
		append(buf, len, kRowChars, " ");
		// `:84` -- the destination's short name, which is a `modulation/params/` table rather than a
		// `util/` one, so it is called rather than transcribed.
		const char* dest_name = params::getPatchedParamShortName(desc.getJustTheParam(), ctx.mod);
		append(buf, len, kRowChars, dest_name != nullptr ? std::string_view{dest_name} : std::string_view{});
		rows[n].label = std::string_view{buf, len};
		rows[n].selectable = true;
		n++;
	}

	// `PatchCables::drawPixelsForOled` (`:91-93`) is `drawItemsForOled(options, ...)`, i.e. exactly
	// the vertical list `ui::oled::list` is the port of -- including `selectEncoderAction`'s clamp
	// and scroll (`:95-111`), which `list` already owns.
	state.render_resp =
	    deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), state.list[depth_of(state)],
	                           std::span<const deluge::ui::oled::ListRow>{rows, n}, state.scroll[depth_of(state)]);
	if (n == 0 && f.select_pressed()) {
		// `selectButtonPress`'s "there were no items" arm (`:168-172`): with nothing to select, the
		// press goes up a level -- `MenuItem::selectButtonPress`'s own `return nullptr`. `list` never
		// reports `entered` on an empty span, so this is the only way that press is seen.
		state.render_resp.entered = true;
	}
}

deluge::ui::shell::Result PatchCablesItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                  const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                  const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// SourceSelectionItem -- `menu_item::SourceSelection` and its two subclasses.

namespace {

/// @brief The sources this picker may offer, in `kSourceMenuContents` order.
/// @return How many were written to @p out.
size_t allowed_sources(const MenuContext& ctx, ParamDescriptor destination, std::span<PatchSource> out) {
	size_t n = 0;
	for (const PatchSource s : kSourceMenuContents) {
		if (n >= out.size()) {
			break;
		}
		if (source_is_allowed(ctx, s, destination)) {
			out[n++] = s;
		}
	}
	return n;
}

} // namespace

void SourceSelectionItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// The half of `PatchedParam::selectButtonPress` a `SelectChainFn` cannot do
	// (`gui/menu_item/patched_param.cpp:38`: `soundEditor.patchingParamSelected = this->getP()`).
	// The node the chain came from is `path[depth - 1]` -- `enter_child` has just pushed this page
	// under it -- and its `bind.param.index` is `getP()`.
	//
	// Guarded on `Kind::PATCHED` so the *other* two ways into this page leave the selection alone:
	// `patch_cables_chain` (which sets `patching_param` from the chosen cable) and a re-entry from
	// the range picker below it.
	if (which_ == Which::Regular && state.depth > 0) {
		const Node* from = state.path[state.depth - 1];
		if (from != nullptr && from->bind.param.kind == params::Kind::PATCHED) {
			state.patching_param = from->bind.param.index;
		}
	}

	const ParamDescriptor destination = destination_for(state, which_ == Which::Range);
	PatchSource sources[kNumPatchSources];
	const size_t n = allowed_sources(ctx, destination, sources);

	// `SourceSelection::beginSession`'s forward-entry arm (`source_selection.cpp:118-143`): land on
	// the first source that is already patched to this destination, else on the first allowed one.
	// (The `navigatedBackwardFrom` arm -- "put the cursor back on `s`" -- is what `MenuScene::ascend`
	// gives for free by never calling this hook on the way back, exactly as for `PatchCablesItem`.)
	int32_t cursor = 0;
	for (size_t i = 0; i < n; i++) {
		if (source_is_patched(ctx, sources[i], destination)) {
			cursor = static_cast<int32_t>(i);
			break;
		}
	}
	state.list[depth_of(state)] = deluge::ui::ListState{};
	state.list[depth_of(state)].cursor = static_cast<int16_t>(cursor);
	state.scroll[depth_of(state)] = deluge::ui::ScrollText{};
	if (n != 0) {
		PatchSource& chosen = which_ == Which::Range ? state.patch_range_source : state.patch_source;
		chosen = sources[cursor];
	}
}

void SourceSelectionItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	draw_title_for(f, state);

	const ParamDescriptor destination = destination_for(state, which_ == Which::Range);
	PatchSource sources[kNumPatchSources];
	const size_t n = allowed_sources(ctx, destination, sources);

	deluge::ui::oled::ListRow rows[kNumPatchSources];
	deluge::ui::shell::TextScratch scratch[kNumPatchSources];
	for (size_t i = 0; i < n; i++) {
		rows[i].label = deluge_profile().text_or_digits(source_display_string(sources[i]), scratch[i]);
		// `shouldDrawDotOnValue` (`source_selection.cpp:52-57`): legacy marks a source that already
		// has a cable to this destination with a dot. The nearest thing this layer draws without a
		// frame primitive is the menu-row checkbox, so that is what marks it.
		rows[i].icon =
		    source_is_patched(ctx, sources[i], destination) ? &deluge::gfx::oled::icons::checkedBox : nullptr;
		rows[i].selectable = true;
	}

	// `SourceSelection::drawPixelsForOled` (`:59-108`) hand-builds a three-row window over the
	// *unfiltered* source table, skipping disallowed entries as it walks. This port filters first
	// and hands `list` the allowed sources only, which shows the same rows in the same order and
	// gets the windowing, the clamp and `selectEncoderAction`'s allowed-only walk (`:150-176`) from
	// the widget instead of re-deriving them.
	state.render_resp =
	    deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), state.list[depth_of(state)],
	                           std::span<const deluge::ui::oled::ListRow>{rows, n}, state.scroll[depth_of(state)]);

	// `selectEncoderAction`'s `s = sourceMenuContents[newValue]` (`:172`): the cursor IS the value,
	// so it is republished every frame rather than only when it moves.
	const int32_t cursor = state.list[depth_of(state)].cursor;
	if (n != 0 && cursor >= 0 && static_cast<size_t>(cursor) < n) {
		PatchSource& chosen = which_ == Which::Range ? state.patch_range_source : state.patch_source;
		chosen = sources[cursor];
	}
	if (n == 0 && f.select_pressed()) {
		state.render_resp.entered = true; // nothing to pick: the press goes up, as an empty list must
	}
}

deluge::ui::shell::Result SourceSelectionItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                      const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                      const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// PatchCableStrengthItem -- `menu_item::PatchCableStrength` and its three subclasses.

namespace {

/// @brief `PatchCableStrength::getS()` -- `Regular`/`Fixed` edit the regular source's cable,
///        `Range` the range source's (`patch_cable_strength/regular.cpp:42-44`, `range.cpp:38-40`).
PatchSource strength_source(const MenuState& state, PatchCableStrengthItem::Which which) {
	return which == PatchCableStrengthItem::Which::Range ? state.patch_range_source : state.patch_source;
}

/// @brief `PatchCableStrength::getDestinationDescriptor()`.
ParamDescriptor strength_destination(const MenuState& state, PatchCableStrengthItem::Which which) {
	return destination_for(state, which == PatchCableStrengthItem::Which::Range);
}

/// @brief `PatchCableStrength::readCurrentValue` (`patch_cable_strength.cpp:192-206`).
int32_t strength_read(const MenuContext& ctx, const MenuState& state, PatchCableStrengthItem::Which which) {
	PatchCableSet* set = cable_set(ctx);
	if (set == nullptr) {
		return 0;
	}
	const uint8_t index = set->getPatchCableIndex(strength_source(state, which), strength_destination(state, which));
	if (index == kNoSelection) {
		return 0; // `:195-198` -- no cable yet reads as zero depth
	}
	return cable_value_to_menu(set->patchCables[index].param.getCurrentValue());
}

} // namespace

ParamDescriptor PatchCableStrengthItem::learn_descriptor(const MenuContext& /*ctx*/, const MenuState& state) const {
	ParamDescriptor desc{};
	if (which_ == Which::Range) {
		// `Range::getLearningThing` (`patch_cable_strength/range.cpp:25-30`).
		desc.setToHaveParamAndTwoSources(state.patching_param, state.patch_source, state.patch_range_source);
	}
	else {
		// `Regular::getLearningThing` (`patch_cable_strength/regular.cpp:30-34`); `Fixed` inherits it.
		desc.setToHaveParamAndSource(state.patching_param, state.patch_source);
	}
	return desc;
}

void PatchCableStrengthItem::on_focus(const MenuContext& ctx, MenuState& state) const {
	// `PatchCables::selectButtonPress`'s own assignments (`patch_cables.cpp:177-189`):
	// `soundEditor.patchingParamSelected = dest`, `source_selection::regularMenu.s = cable->from`,
	// and for a cable whose destination is itself a cable, `rangeMenu.s = cable->from` with
	// `regularMenu.s` taking the top-level source instead. A `SelectChainFn` is handed a
	// **`const MenuState&`** and cannot write any of it, so -- exactly as for
	// `SourceSelectionItem::on_focus` -- the target does it, recognising where it came from by the
	// node `enter_child` has just pushed it under.
	// Recognised by the parent's *item*, not by its node address: `nodes::patch_cables` lives in
	// `sound_root.h`, which includes this header, and the identity is the item either way.
	const Node* from = state.depth > 0 ? state.path[state.depth - 1] : nullptr;
	if (from != nullptr && from->custom == &kPatchCablesItem) {
		PatchCableSet* set = cable_set(ctx);
		const int32_t cursor = state.list[state.depth - 1].cursor;
		if (set != nullptr && cursor >= 0 && cursor < set->numPatchCables) {
			const PatchCable& cable = set->patchCables[cursor];
			const ParamDescriptor desc = cable.destinationParamDescriptor;
			state.patching_param = static_cast<uint8_t>(desc.getJustTheParam());
			if (desc.isJustAParam()) {
				state.patch_source = cable.from; // `:180-183`
			}
			else {
				state.patch_source = desc.getTopLevelSource(); // `:184-189`
				state.patch_range_source = cable.from;
			}
		}
	}

	if (which_ == Which::Fixed) {
		// `Fixed::checkPermissionToBeginSession` (`patch_cable_strength/fixed.cpp:28-33`): the whole
		// point of the class is that it installs its own destination and source before behaving as a
		// `Regular`.
		state.patching_param = param_;
		state.patch_source = source_;
	}
	else if (which_ == Which::Regular && state.patching_param == params::GLOBAL_VOLUME_POST_FX) {
		// `Regular::checkPermissionToBeginSession` (`patch_cable_strength/regular.cpp:46-65`): the
		// three volume params are one destination to the user, so a source that cannot reach POST_FX
		// is retried against POST_REVERB_SEND and then LOCAL_VOLUME before the page gives up.
		auto* param_manager = static_cast<ParamManagerForTimeline*>(ctx.params);
		const auto disallowed = [&](int32_t p) {
			return ctx.sound == nullptr || param_manager == nullptr
			       || ctx.sound->maySourcePatchToParam(state.patch_source, static_cast<uint8_t>(p), param_manager)
			              == PatchCableAcceptance::DISALLOWED;
		};
		if (disallowed(params::GLOBAL_VOLUME_POST_FX)) {
			state.patching_param = params::GLOBAL_VOLUME_POST_REVERB_SEND;
			if (disallowed(params::GLOBAL_VOLUME_POST_REVERB_SEND)) {
				state.patching_param = params::LOCAL_VOLUME;
			}
		}
	}

	// `PatchCableStrength::beginSession` (`patch_cable_strength.cpp:45-67`): the polarity the page
	// shows is the cable's own, or -- when there is no cable yet -- the source's default. Legacy
	// also calls `setPatchCablePolarity(polarity_in_the_ui_)` here, which is a no-op in the
	// no-cable case and a write-back of what it just read in the other; the port skips the
	// round trip and keeps the read.
	const PatchSource source = strength_source(state, which_);
	PatchCableSet* set = cable_set(ctx);
	const uint8_t existing =
	    set != nullptr ? set->getPatchCableIndex(source, strength_destination(state, which_)) : uint8_t{kNoSelection};
	state.patch_polarity =
	    existing != kNoSelection ? set->patchCables[existing].polarity : PatchCable::getDefaultPolarity(source);

	// `Decimal::beginSession` plus `getDefaultEditPos()` (`patch_cable_strength.h:39`): the cursor
	// starts on the hundredths' place.
	state.edit_int = strength_read(ctx, state, which_);
	state.decimal_pos = 2;
	state.cursor_blink = deluge::ui::Blink{};
}

/// @name This page's layout, which is legacy's own -- all of it, in one place.
///
/// **The whole page is frame-local**, i.e. every row below is measured from the top of the visible
/// panel, which is what `f.origin.y` carries. On the reference geometry (`OLED_MAIN_TOPMOST_PIXEL`
/// is 5) each of them lands on exactly the canvas row `PatchCableStrength::renderOLED` names, and
/// on the taller panel the page hugs the visible top as one piece instead of splitting in half.
///
/// That last part is the fix for E2 task 12's re-ruling. The page used to be laid out two ways at
/// once: the diagram and the switch frame-local, but the number on `decimal_value`'s
/// **canvas-absolute** rows 20/41 -- which are `Decimal::drawPixelsForOled`'s constants for the
/// *full-panel* number page, not this page's. Those rows sit 10 rows below legacy's number, which
/// left no room under it, which is why the switch had been pushed to the top of the frame, above
/// the number, where legacy puts it below. So the page had legacy's *face* for the number (task 12)
/// and the toolkit's *position* for the switch -- an arrangement neither argument supported.
/// `DecimalLayout` now carries the rows as well as the face (`ui/oled/values.h`), the number goes
/// where `patch_cable_strength.cpp` puts it, and the switch follows it down to legacy's `startY`.
///
/// Legacy's four constants, and what each becomes here:
///
///   * the diagram's first row, `extraY + OLED_MAIN_TOPMOST_PIXEL` (`:87`) -- canvas 6; here the
///     rect's own y of 0, plus `routing_chain`'s internal `+1`;
///   * the number's top, `textY = extraY + OLED_MAIN_TOPMOST_PIXEL + 4` (`:175`) -- canvas 10;
///     here `kValueTextY` = 5;
///   * the cursor, `textY + digitHeight + 1` (`:180`) -- canvas 24; here `kValueCursorY` = 19;
///   * the switch's slot band, `startY = 34` (`:131`) -- canvas 34; here `kPolaritySwitchY` = 29.
///
/// @{

/// @brief How many columns of the page the routing diagram owns.
///
/// Everything left of this is the diagram's; everything right of it is the value's. Legacy needs no
/// such split because its own number is right-aligned at the panel's edge, well clear of a diagram
/// that runs down the left; `decimal_value` anchors horizontally within the rect it is handed, so
/// handing it the right-hand rect is how the two stay apart.
constexpr int32_t kRoutingDiagramWidth = 58;

/// @brief The number's top row and its cursor's, frame-local -- `textY` and
///        `textY + digitHeight + 1` (`patch_cable_strength.cpp:175`, `:180 renderOLED`), with
///        `digitHeight = kTextBigSizeY` (13).
/// @{
constexpr int32_t kValueTextY = 5;
constexpr int32_t kValueCursorY = kValueTextY + deluge::gfx::oled::kTextBigSizeY + 1;
/// @}

/// @brief The polarity switch's slot band, frame-local -- legacy's `startY = 34`
///        (`patch_cable_strength.cpp:131 renderOLED`).
///
/// `segmented_switch` inks two rows *above* this one for its bracket (`ui/oled/routing.h`), so the
/// page's real ink starts at 27; the number's cursor ends at `kValueCursorY + 1` = 20. Six clear
/// rows between them, which is legacy's own gap.
constexpr int32_t kPolaritySwitchY = 29;
/// @}

/// @brief The polarity switch's two slot widths -- `biSlotWidth` / `uniSlotWidth`
///        (`patch_cable_strength.cpp:128-129`).
/// @{
constexpr std::array<int32_t, 2> kPolarityWidths{{25, 28}};
constexpr std::array<int32_t, 1> kNoPolarityWidth{{53}};
/// @}

void PatchCableStrengthItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	const PatchSource source = strength_source(state, which_);
	const ParamDescriptor destination = strength_destination(state, which_);
	const deluge::gfx::oled::Rect bounds = f.bounds();

	// `PatchCableStrength::renderOLED`'s first block (`patch_cable_strength.cpp:79-123`), through
	// the toolkit widget that ports it (`ui/oled/routing.h`). The stage order is legacy's: the
	// cable's own source first, then -- for a cable modulating another cable's depth -- that outer
	// cable's source, then the destination param.
	//
	// **No title.** Legacy draws none on this page: `renderOLED` replaces `drawPixelsForOled`
	// wholesale, and the diagram is the page's identity.
	const char* dest_name = params::getPatchedParamShortName(destination.getJustTheParam(), ctx.mod);
	std::array<std::string_view, deluge::ui::oled::kMaxRoutingStages> stages{};
	size_t n = 0;
	stages[n++] = source_short_name(source);
	if (!destination.isJustAParam()) {
		stages[n++] = source_short_name(destination.getTopLevelSource());
	}
	stages[n++] = dest_name != nullptr ? std::string_view{dest_name} : std::string_view{};
	deluge::ui::oled::routing_chain(f, deluge::gfx::oled::Rect{0, 0, kRoutingDiagramWidth, bounds.h},
	                                std::span<const std::string_view>{stages.data(), n});

	PatchCableSet* set = cable_set(ctx);
	const uint8_t index = set != nullptr ? set->getPatchCableIndex(source, destination) : uint8_t{kNoSelection};

	// `renderOLED`'s polarity switcher (`:125-157`), through `segmented_switch`. The band's left
	// edge is `bounds.w - 55`, which is legacy's own `startX` of 73 on a 128-column panel, and its
	// row is `kPolaritySwitchY` -- legacy's `startY` of 34, expressed frame-local (see
	// `kRoutingDiagramWidth`). Below the number, which is where legacy puts it.
	const deluge::gfx::oled::Rect switch_area{bounds.w - 55, kPolaritySwitchY, 53, deluge::gfx::oled::kTextSpacingY};
	if (PatchCable::hasPolarity(source)) {
		static constexpr std::array<std::string_view, 2> kPolarityLabels{{"BI", "UNI"}}; // `:143-145`
		deluge::ui::oled::segmented_switch(f, switch_area, kPolarityLabels, kPolarityWidths,
		                                   state.patch_polarity == Polarity::UNIPOLAR ? 1 : 0);
	}
	else {
		// `:151-156` -- a param with no polarity shows only the default one, full width, uninverted.
		const std::string_view only = polarityToString(PatchCable::getDefaultPolarity(source));
		deluge::ui::oled::segmented_switch(f, switch_area, std::span<const std::string_view>{&only, 1},
		                                   kNoPolarityWidth, -1);
	}

	// `getMinValue`/`getMaxValue`/`getNumDecimalPlaces` (`patch_cable_strength.h:36-38`).
	constexpr IntRange kRange{kMinMenuPatchCableValue, kMaxMenuPatchCableValue, 1};
	const deluge::gfx::oled::Rect value_rect{kRoutingDiagramWidth, 0, bounds.w - kRoutingDiagramWidth, bounds.h};
	// **The 13px face, right-aligned** -- `PatchCableStrength::renderOLED`'s own
	// `digitWidth = kTextBigSpacingX`, `digitHeight = kTextBigSizeY`, `rightPadding = 3`,
	// `drawStringAlignRight(..., OLED_MAIN_WIDTH_PIXELS - rightPadding)`
	// (`patch_cable_strength.cpp:172-177`).
	//
	// E2 task 12 fix round 1. Task 9 left this on the toolkit's default huge face, on the reasoning
	// that the difference was cosmetic; it is not. At 18px per glyph the string runs off the panel
	// for **every** value of magnitude 10 or more, and further for every negative one -- most of the
	// +/-50.00 range (`kMinMenuPatchCableValue`/`kMaxMenuPatchCableValue`), on the single number this
	// page exists to edit, while the mod-matrix list one level up renders the same value correctly.
	// At 11px the widest string this range reaches (`-50.00`, five glyphs and a half-width period =
	// 60px) sits comfortably inside the 70px column left of the diagram.
	static constexpr deluge::ui::oled::DecimalLayout kValueLayout{.font = deluge::gfx::oled::FontId::MetricBold13,
	                                                              .right_pad = 3,
	                                                              .text_dy = kValueTextY,
	                                                              .cursor_dy = kValueCursorY};
	const deluge::ui::Response r =
	    deluge::ui::oled::decimal_value(f, value_rect, state.edit_int, kRange, /*places=*/2, state.decimal_pos,
	                                    state.cursor_blink, /*focused=*/true, &kValueLayout);
	state.render_resp = r;
	if (r.changed) {
		state.value_changed = true;
		state.value_node = focused_of(state);
	}

	// `PatchCableStrength::selectEncoderAction`'s polarity arm (`:325-336`). Legacy gates it on the
	// select encoder being *held*; a frame carries no held-button level for that encoder, so this
	// port uses SHIFT + turn -- the one modifier `FrameContext` does expose (`f.shift()`), and the
	// same modifier legacy's own `MultiRange` uses for its second gesture.
	const int8_t turn = f.turn();
	if (turn != 0 && PatchCable::hasPolarity(source) && f.shift()) {
		// `polarity_in_the_ui_` first, then `setPatchCablePolarity` (`:328`), which is a no-op
		// while no cable exists -- and `commit` is what applies it to one it creates.
		state.patch_polarity = turn > 0 ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
		if (index != kNoSelection) {
			set->patchCables[index].polarity = state.patch_polarity;
		}
		deluge::notify::dirty().raise(deluge::notify::Dirty::Oled);
	}

	if (f.select_pressed()) {
		state.render_resp.entered = true; // -> `patch_cable_strength_chain`
	}
}

void PatchCableStrengthItem::commit(const MenuContext& ctx, MenuState& state) const {
	// `PatchCableStrength::writeCurrentValue` (`patch_cable_strength.cpp:231-248`), through the one
	// place in this layer that may assemble a model stack.
	//
	// Whether the cable existed is read *before* the write, because the write is what may create
	// one: legacy asks the same question with its own `patch_cable_exists_` flag, seeded in
	// `beginSession` (`:57-64`) and cleared by `readCurrentValue` when the cable is gone (`:197`).
	const PatchSource source = strength_source(state, which_);
	PatchCableSet* set = cable_set(ctx);
	const bool existed =
	    set != nullptr && set->getPatchCableIndex(source, strength_destination(state, which_)) != kNoSelection;

	if (!bindings::patch_cable_write(ctx, learn_descriptor(ctx, state), state.edit_int)) {
		return;
	}

	// `getModelStack(..., allowCreation = true)`'s tail (`:223-227`): a cable this write has just
	// *created* takes the polarity the page was showing.
	//
	// What that is worth, precisely: `PatchCableSet` already gives a new cable
	// `setDefaultPolarity()` (`modulation/patch/patch_cable_set.cpp:438`), which is
	// `getDefaultPolarity(from)` (`patch_cable.cpp:74-76`), so the *default* is never at risk. What
	// would be lost without this is a polarity the user chose while no cable existed -- legacy's
	// `polarity_in_the_ui_`, this port's `MenuState::patch_polarity`.
	if (!existed && PatchCable::hasPolarity(source)) {
		const uint8_t created = set != nullptr ? set->getPatchCableIndex(source, strength_destination(state, which_))
		                                       : uint8_t{kNoSelection};
		if (created != kNoSelection) {
			set->patchCables[created].polarity = state.patch_polarity; // `setPatchCablePolarity` (`:371-381`)
		}
	}
	if (updates_reverb_) {
		// `sidechain::VolumeShortcut::writeCurrentValue` (`sidechain/volume.h:26-29`).
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	}
}

deluge::ui::shell::Result PatchCableStrengthItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                         const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                         const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	return {};
}

// ---------------------------------------------------------------------------------------------
// The chain hooks.

const Node* patched_param_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &source_selection_regular; // `patched_param.cpp:39`
}

const Node* source_selection_regular_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &patch_cable_strength_regular; // `source_selection/regular.cpp:33`
}

const Node* source_selection_range_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &patch_cable_strength_range; // `source_selection/range.cpp:33`
}

const Node* patch_cable_strength_chain(const MenuContext& /*ctx*/, const MenuState& /*state*/) {
	return &source_selection_range; // `patch_cable_strength.cpp:308`
}

const Node* patch_cables_chain(const MenuContext& ctx, const MenuState& state) {
	// `PatchCables::selectButtonPress` (`patch_cables.cpp:164-190`). The cable the cursor is on
	// decides: a cable whose destination is a bare param opens the regular strength page, one whose
	// destination is itself a cable opens the range strength page.
	PatchCableSet* set = cable_set(ctx);
	const int32_t cursor = state.list[state.depth < kMenuDepth ? state.depth : 0].cursor;
	if (set == nullptr || cursor < 0 || cursor >= set->numPatchCables) {
		return nullptr; // `:168-172` -- no items, so the press ascends
	}
	return set->patchCables[cursor].destinationParamDescriptor.isJustAParam() ? &patch_cable_strength_regular
	                                                                          : &patch_cable_strength_range;
}

// ---------------------------------------------------------------------------------------------
// The nodes. Defined here rather than in the header -- see `patch_cables.h` for why (the chain is
// cyclic, so at least one edge has to be an `extern` address rather than a `constexpr` initialiser).

const Node source_selection_regular =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_WITH), &kSourceSelectionRegularItem),
                      &source_selection_regular_chain, kSourceSelectionRegularChain);

const Node patch_cable_strength_regular =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_WITH), &kPatchCableStrengthRegularItem),
                      &patch_cable_strength_chain, kPatchCableStrengthChain);

const Node source_selection_range =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_DEPTH), &kSourceSelectionRangeItem),
                      &source_selection_range_chain, kSourceSelectionRangeChain);

const Node patch_cable_strength_range =
    with_select_chain(custom(sid(String::STRING_FOR_MODULATE_DEPTH), &kPatchCableStrengthRangeItem),
                      &patch_cable_strength_chain, kPatchCableStrengthChain);

} // namespace deluge::scenes::menu::nodes
