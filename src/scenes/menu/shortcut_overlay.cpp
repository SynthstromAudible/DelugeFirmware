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
/// @brief The SHIFT shortcut colour overlay. See `shortcut_overlay.h`.

#include "scenes/menu/shortcut_overlay.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/menu_core.h"
#include "scenes/menu/nodes/patching.h"
#include "scenes/menu/walk.h"

namespace deluge::scenes::menu {

namespace {

namespace c = deluge::gui::colours;
namespace params = deluge::modulation::params;

constexpr RGB kSample = c::green;
constexpr RGB kMainOsc = c::blue;
constexpr RGB kFmOsc = c::purple;
constexpr RGB kSynthGlobal = c::amber;
constexpr RGB kWaveshape = c::red;
constexpr RGB kNoise = c::darkblue;
constexpr RGB kEnv = c::green;
constexpr RGB kLfo = c::pastel::green;
constexpr RGB kArp = c::pastel::blue;
constexpr RGB kSidechain = c::yellow_orange;
constexpr RGB kFilter = c::red_orange;
constexpr RGB kEq = c::magenta;
constexpr RGB kFx = c::orange;
constexpr RGB kReverb = c::red;
constexpr RGB kDelay = c::cyan;
constexpr RGB kNone = c::black;
constexpr RGB kMeta = c::white;

} // namespace

// clang-format off
const std::array<std::array<RGB, kShortcutRows>, kShortcutCols> kShortcutColours = {{
    {{kSample,      kSample,      kSample,      kSample,      kSample,      kSample,    kSample,    kSample}},
    {{kSample,      kSample,      kSample,      kSample,      kSample,      kSample,    kSample,    kSample}},
    {{kMainOsc,     kMainOsc,     kMainOsc,     kMainOsc,     kMainOsc,     kMainOsc,   kMainOsc,   kNoise}},
    {{kMainOsc,     kMainOsc,     kMainOsc,     kMainOsc,     kMainOsc,     kMainOsc,   kMainOsc,   kMainOsc}},
    {{kFmOsc,       kFmOsc,       kNone,        kNone,        kFmOsc,       kFmOsc,     kNone,      kMeta}},
    {{kFmOsc,       kFmOsc,       kNone,        kNone,        kFmOsc,       kFmOsc,     kFmOsc,     kSynthGlobal}},
    {{kSynthGlobal, kSynthGlobal, kSynthGlobal, kSynthGlobal, kSynthGlobal, kWaveshape, kWaveshape, kWaveshape}},
    {{kSynthGlobal, kSynthGlobal, kSynthGlobal, kSynthGlobal, kSynthGlobal, kWaveshape, kWaveshape, kWaveshape}},
    {{kEnv,         kEnv,         kEnv,         kEnv,         kFilter,      kFilter,    kFilter,    kFilter}},
    {{kEnv,         kEnv,         kEnv,         kEnv,         kFilter,      kFilter,    kFilter,    kFilter}},
    {{kSidechain,   kSidechain,   kSidechain,   kSidechain,   kSidechain,   kSidechain, kEq,        kEq}},
    {{kArp,         kArp,         kArp,         kArp,         kArp,         kMeta,      kEq,        kEq}},
    {{kLfo,         kLfo,         kLfo,         kFx,          kFx,          kFx,        kFx,        kFx}},
    {{kLfo,         kLfo,         kLfo,         kReverb,      kReverb,      kReverb,    kReverb,    kReverb}},
    {{kDelay,       kDelay,       kDelay,       kDelay,       kDelay,       kNone,      kNone,      kNone}},
    {{kNone,        kSynthGlobal, kSynthGlobal, kSynthGlobal, kNone,        kNone,      kNone,      kNone}},
}};
// clang-format on

bool overlay_shown(const MenuContext& ctx, bool shift) {
	return ctx.shortcut_overlay && shift && ctx.root != MenuContext::Root::Settings
	       && ctx.root != MenuContext::Root::NoteEditor && ctx.root != MenuContext::Root::NoteRowEditor;
}

bool other_source_pad_held(const deluge::ui::PadMask& held, uint8_t x, uint8_t y) {
	for (uint8_t sx = kShortcutCols - 2; sx < kShortcutCols; sx++) {
		for (uint8_t sy = 0; sy < kShortcutRows; sy++) {
			if (is_patching_source_pad(sx, sy) && (sx != x || sy != y) && held.test(sx, sy)) {
				return true;
			}
		}
	}
	return false;
}

void paint_shortcut_colours(deluge::ui::grid::GridFrame& f, const MenuContext& ctx) {
	for (uint8_t x = 0; x < kShortcutCols; x++) {
		// `isRelevant(forThing, item, x % 2)`: an oscillator pair's two columns are asked for their own source.
		MenuContext source_ctx = ctx;
		source_ctx.source_index = static_cast<int8_t>(x % 2);
		for (uint8_t y = 0; y < kShortcutRows; y++) {
			const Node* node = shortcut_node(ctx.root, false, x, y);
			const bool relevant = node != nullptr && bindings::relevant(*node, source_ctx);
			(void)f.pad(x, y, relevant ? kShortcutColours[x][y] : kNone);
		}
	}
}

void paint_source_column(deluge::ui::grid::GridFrame& f, const MenuContext& ctx, const MenuState& st,
                         const Node* focused) {
	// A patched param, or a cable page patching `patching_param`; and a sound to patch on.
	if (focused == nullptr || (ctx.root != MenuContext::Root::Sound && ctx.root != MenuContext::Root::Drum)) {
		return;
	}
	int32_t param = 0;
	if (focused->bind.param.kind == params::Kind::PATCHED) {
		param = focused->bind.param.index;
	}
	else if (focused->custom != nullptr && focused->custom->is_patch_cable()) {
		param = st.walk.patching_param;
	}
	else {
		return;
	}
	const bool is_volume = param == params::GLOBAL_VOLUME_POST_FX || param == params::GLOBAL_VOLUME_POST_REVERB_SEND
	                       || param == params::LOCAL_VOLUME;
	const auto allowed = [&](PatchSource source, bool edits_range) {
		if (source >= kLastPatchSource) {
			return false;
		}
		if (edits_range) {
			// `PatchCableStrength::checkPermissionToBeginSession` for a cable's range.
			return source < kFirstLocalSource || param < params::FIRST_GLOBAL;
		}
		// The volume triple takes the destination the source can reach.
		if (is_volume) {
			return patching::source_may_patch(ctx, source, params::GLOBAL_VOLUME_POST_FX)
			       || patching::source_may_patch(ctx, source, params::LOCAL_VOLUME)
			       || patching::source_may_patch(ctx, source, params::GLOBAL_VOLUME_POST_REVERB_SEND);
		}
		return patching::source_may_patch(ctx, source, param);
	};
	for (uint8_t sx = 0; sx < 2; sx++) {
		const auto x = static_cast<uint8_t>(kShortcutCols - 2 + sx);
		for (uint8_t y = 0; y < kShortcutRows; y++) {
			const bool previous_held = other_source_pad_held(f.held_pads(), x, y);
			// The items from the focused one up answer what the pad patches to, while they defer.
			SourceTarget target = SourceTarget::Parameter;
			for (int32_t depth = st.depth; depth >= 0; depth--) {
				const Node* node = st.path[depth];
				if (node != nullptr && node->kind == Kind::Submenu) {
					node = is_horizontal(ctx, *node) ? child_at(ctx, *node, st.columns[depth].focused) : nullptr;
				}
				target = node != nullptr && node->custom != nullptr ? node->custom->source_target(previous_held)
				                                                    : SourceTarget::Parameter;
				if (target != SourceTarget::Parent) {
					break;
				}
			}
			const bool edits_range = target == SourceTarget::Range;
			if (allowed(patching::mod_source_shortcut(sx, y, false), edits_range)
			    || allowed(patching::mod_source_shortcut(sx, y, true), edits_range)) {
				(void)f.pad(x, y, c::blue);
			}
		}
	}
}

void render_overlay(deluge::ui::grid::GridFrame& f, const MenuContext& ctx, const MenuState& st, const Node* focused) {
	paint_shortcut_colours(f, ctx);
	paint_source_column(f, ctx, st, focused);
}

} // namespace deluge::scenes::menu
