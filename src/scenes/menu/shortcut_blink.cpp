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

#include "scenes/menu/shortcut_blink.h"

#include "scenes/menu/menu_state.h"
#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/patching.h"

#include <algorithm>
#include <iterator>
#include <span>

namespace deluge::scenes::menu {

using deluge::ui::grid::FlashColour;

namespace {

/// @brief Whether @p node's shortcut pad blinks while it is focused.
///
/// `SoundEditor::updatePadLightsFor` sets up no blink for the name
/// editors and the sample-marker editors, although the shortcut tables hold them.
/// @param node The focused node.
/// @return False for those nodes, true for every other.
bool blinks_shortcut(const Node& node) {
	constexpr const Node* kNoBlinkNodes[] = {&nodes::edit_name,          &nodes::drum_name_edit, &nodes::sample0_start,
	                                         &nodes::sample0_end,        &nodes::sample1_start,  &nodes::sample1_end,
	                                         &nodes::audio_clip_waveform};
	return std::ranges::find(kNoBlinkNodes, &node) == std::ranges::end(kNoBlinkNodes);
}

/// The flash colour of a pad on the second shortcut layer, or the first's.
FlashColour layer_colour(bool second_layer) {
	return second_layer ? FlashColour::Yellow : FlashColour::Default;
}

/// @brief The sound arm's search for @p item's own pad (`updatePadLightsFor`, `sound_editor.cpp:1150-1168`).
///
/// Cell by cell, column-major: a second-layer cell ends the search at once; a first-layer cell takes the pad, except
/// that an odd column after an earlier pad ends the search while source 1 is not the one edited -- the two
/// oscillators' columns share their items. Main's reverb-sidechain carve-out (`x == 10 && y < 6 &&
/// editingReverbSidechain()`) is not here: nothing selects the reverb's sidechain for editing, so it never fires.
ParamBlink own_pad(const MenuContext& ctx, const Node& item) {
	const std::span<const Node* const> first = shortcut_layer(ctx.root, false);
	const std::span<const Node* const> second = shortcut_layer(ctx.root, true);
	ParamBlink found{};
	for (uint8_t x = 0; x < kShortcutCols; x++) {
		for (uint8_t y = 0; y < kShortcutRows; y++) {
			const size_t cell = (size_t{y} * kShortcutCols) + x;
			if (second.data() != first.data() && second[cell] == &item) {
				return ParamBlink{.x = x, .y = y, .valid = true, .freq_mask = 0, .colour = FlashColour::Yellow};
			}
			if (first[cell] == &item) {
				if (found.valid && (x & 1) != 0 && ctx.source_index == 0) {
					return found;
				}
				found = ParamBlink{.x = x, .y = y, .valid = true, .freq_mask = 0, .colour = FlashColour::Default};
			}
		}
	}
	return found;
}

/// Whether @p node is the patched param @p p's own item.
bool is_patched_param(const Node* node, uint8_t p) {
	return node != nullptr && node->bind.param.kind == deluge::modulation::params::Kind::PATCHED
	       && node->bind.param.index == p;
}

/// @brief The pad of patched param @p p, every fourth cycle -- `SoundEditor::findPatchedParam` (`sound_editor.cpp:
///        1011-1032`): cell by cell, column-major, the first layer before the second at each cell.
ParamBlink patched_param_pad(MenuContext::Root root, uint8_t p) {
	if (p == kNoBlink) {
		return {};
	}
	const std::span<const Node* const> first = shortcut_layer(root, false);
	const std::span<const Node* const> second = shortcut_layer(root, true);
	for (uint8_t x = 0; x < kShortcutCols; x++) {
		for (uint8_t y = 0; y < kShortcutRows; y++) {
			const size_t cell = (size_t{y} * kShortcutCols) + x;
			for (const bool on_second : {false, true}) {
				if (is_patched_param((on_second ? second : first)[cell], p)) {
					return ParamBlink{.x = x, .y = y, .valid = true, .freq_mask = 3, .colour = layer_colour(on_second)};
				}
			}
		}
	}
	return {};
}

/// @brief How @p item blinks @p source's pad: its own answer for a bespoke item, and for a patched param every fourth
///        cycle when the source is patched to it (`PatchedParam::shouldBlinkPatchingSourceShortcut`).
uint8_t source_answer(const MenuContext& ctx, const MenuState& state, const Node& item, PatchSource source,
                      FlashColour& colour) {
	if (item.custom != nullptr) {
		return item.custom->source_blink(ctx, state, source, colour);
	}
	if (item.bind.param.kind == deluge::modulation::params::Kind::PATCHED) {
		return patching::source_is_patched(ctx, source, deluge::synth::CableTarget::param(item.bind.param.index))
		           ? 3
		           : kNoBlink;
	}
	return kNoBlink;
}

/// @brief `SoundEditor::updateSourceBlinks`: each source pad's first-layer source, then
///        its second-layer one; the lower frequency wins, a second-layer win flashes yellow, and an equal frequency
///        alternates.
void plan_sources(const MenuContext& ctx, const MenuState& state, const Node& item, BlinkPlan& plan) {
	for (uint8_t sx = 0; sx < 2; sx++) {
		for (uint8_t y = 0; y < kShortcutRows; y++) {
			SourceBlink& blink = plan.sources[sx][y];
			blink = SourceBlink{};
			if (const PatchSource s = patching::mod_source_shortcut(sx, y, false); s < kLastPatchSource) {
				blink.freq_mask = source_answer(ctx, state, item, s, blink.colour);
			}
			if (const PatchSource s = patching::mod_source_shortcut(sx, y, true); s < kLastPatchSource) {
				FlashColour unused = FlashColour::Default;
				const uint8_t second = source_answer(ctx, state, item, s, unused);
				blink.alternates = second != kNoBlink && second == blink.freq_mask;
				if (second < blink.freq_mask) {
					blink.freq_mask = second;
					blink.colour = FlashColour::Yellow;
				}
			}
		}
	}
}

} // namespace

bool BlinkPlan::any_source() const {
	return std::ranges::any_of(sources, [](const auto& column) {
		return std::ranges::any_of(column, [](const SourceBlink& s) { return s.freq_mask != kNoBlink; });
	});
}

BlinkPlan plan_blinks(const MenuContext& ctx, const MenuState& state, const Node& focused) {
	BlinkPlan plan;
	// `updatePadLightsFor` returns at once under the note and note-row editors and sets nothing up
	// in the settings menu (`sound_editor.cpp:1029-1036`).
	if (ctx.root == MenuContext::Root::Settings || ctx.root == MenuContext::Root::NoteEditor
	    || ctx.root == MenuContext::Root::NoteRowEditor || !blinks_shortcut(focused)) {
		return plan;
	}
	if (ctx.root != MenuContext::Root::Sound && ctx.root != MenuContext::Root::Drum) {
		// The table arms: the item's own pad, every cycle.
		const ShortcutPos pos = shortcut_pos(ctx.root, focused);
		if (pos.valid) {
			plan.param = ParamBlink{
			    .x = pos.x, .y = pos.y, .valid = true, .freq_mask = 0, .colour = layer_colour(pos.second_layer)};
		}
		return plan;
	}
	// The sound arm. The range picker blinks for the item it heads to.
	const Node* item = &focused;
	if (item == &nodes::multi_range && state.walk.pending_after_range != nullptr) {
		item = state.walk.pending_after_range;
	}
	if (item->custom != nullptr) {
		if (const std::optional<ItemBlink> blink = item->custom->own_blink(ctx, state)) {
			return blink->valid ? exclusive_blink(blink->x, blink->y, blink->freq_mask) : BlinkPlan{};
		}
	}
	plan.param = own_pad(ctx, *item);
	if (!plan.param.valid) {
		// Failing that, the patched param the item is patching (`:1171-1183`).
		const uint8_t p = item->custom != nullptr ? item->custom->patched_param_to_blink(ctx, state) : kNoBlink;
		plan.param = patched_param_pad(ctx.root, p);
	}
	// A source never blinks without a param pad (`:1185-1195`).
	if (plan.param.valid) {
		plan_sources(ctx, state, *item, plan);
	}
	return plan;
}

BlinkPlan exclusive_blink(uint8_t x, uint8_t y, uint8_t freq_mask) {
	BlinkPlan plan;
	plan.param = ParamBlink{.x = x, .y = y, .valid = true, .freq_mask = freq_mask, .colour = FlashColour::Default};
	return plan;
}

uint32_t blink_step(uint32_t anchor_ms, uint32_t now_ms) {
	const uint32_t d = now_ms - anchor_ms;
	return 2 * (d / kBlinkCycleMs) + ((d % kBlinkCycleMs) >= kSourceBlinkMs ? 1 : 0);
}

uint32_t ms_to_next_step(uint32_t anchor_ms, uint32_t now_ms) {
	const uint32_t phase = (now_ms - anchor_ms) % kBlinkCycleMs;
	return phase < kSourceBlinkMs ? (kSourceBlinkMs - phase) : (kBlinkCycleMs - phase);
}

void fire_blink_step(deluge::ui::grid::GridFrame& f, const BlinkPlan& plan, uint32_t step) {
	const uint32_t cycle = step >> 1;
	if ((step & 1) != 0) {
		// The param step, 180 ms long.
		if (plan.param.valid && (cycle & plan.param.freq_mask) == 0) {
			f.flash(plan.param.x, plan.param.y, plan.param.colour);
		}
		return;
	}
	// The source step, 20 ms long.
	for (int32_t sx = 0; sx < 2; sx++) {
		for (int32_t y = 0; y < kShortcutRows; y++) {
			const SourceBlink& s = plan.sources[sx][y];
			if (s.freq_mask == kNoBlink || (cycle & s.freq_mask) != 0) {
				continue;
			}
			FlashColour colour = s.colour;
			// The next counter bit above the mask alternates the flash to the second layer's yellow.
			if (s.alternates && (cycle & (uint32_t{s.freq_mask} + 1)) != 0) {
				colour = FlashColour::Yellow;
			}
			f.flash(kShortcutCols - 2 + sx, y, colour);
		}
	}
}

} // namespace deluge::scenes::menu
