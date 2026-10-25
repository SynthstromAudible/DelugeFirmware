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

#include "scenes/menu/node.h"
#include "scenes/menu/nodes/nodes.h"

#include <algorithm>
#include <iterator>

namespace deluge::scenes::menu {

using deluge::ui::grid::FlashColour;

namespace {

/// @brief Whether @p node's shortcut pad blinks while it is focused.
///
/// `SoundEditor::updatePadLightsFor` (`sound_editor.cpp:1036-1040`) sets up no blink for the name
/// editors and the sample-marker editors, although the shortcut tables hold them.
/// @param node The focused node.
/// @return False for those nodes, true for every other.
bool blinks_shortcut(const Node& node) {
	constexpr const Node* kNoBlinkNodes[] = {&nodes::edit_name,          &nodes::drum_name_edit, &nodes::sample0_start,
	                                         &nodes::sample0_end,        &nodes::sample1_start,  &nodes::sample1_end,
	                                         &nodes::audio_clip_waveform};
	return std::ranges::find(kNoBlinkNodes, &node) == std::ranges::end(kNoBlinkNodes);
}

} // namespace

bool BlinkPlan::any_source() const {
	return std::ranges::any_of(sources, [](const auto& column) {
		return std::ranges::any_of(column, [](const SourceBlink& s) { return s.freq_mask != kNoBlink; });
	});
}

BlinkPlan plan_blinks(const MenuContext& ctx, const MenuState& /*state*/, const Node& focused) {
	BlinkPlan plan;
	// `updatePadLightsFor` returns at once under the note and note-row editors and sets nothing up
	// in the settings menu (`sound_editor.cpp:1029-1036`).
	if (ctx.root == MenuContext::Root::Settings || ctx.root == MenuContext::Root::NoteEditor
	    || ctx.root == MenuContext::Root::NoteRowEditor || !blinks_shortcut(focused)) {
		return plan;
	}
	const ShortcutPos pos = shortcut_pos(ctx.root, focused);
	if (pos.valid) {
		plan.param = ParamBlink{
		    .x = pos.x,
		    .y = pos.y,
		    .valid = true,
		    .freq_mask = 0,
		    .colour = pos.second_layer ? FlashColour::Yellow : FlashColour::Default,
		};
	}
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
