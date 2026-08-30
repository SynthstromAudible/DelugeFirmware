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
/// @brief `ActiveScalesItem`'s members. See `scales.h`.

#include "scenes/menu/nodes/scales.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/profile.h"

#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include "gfx/oled/icons.h"

#include "model/scale/preset_scales.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace deluge::scenes::menu::nodes {

namespace {

/// @brief The number of rows both items draw: `PresetScale` 0 .. `LAST_PRESET_SCALE`
///        (`model/scale/preset_scales.h:70-80`).
///
/// `NUM_PRESET_SCALES` is `LAST_PRESET_SCALE + 1`, i.e. the preset scales and nothing after them --
/// `USER_SCALE` and the rest of `NUM_SCALELIKE` are not offered here, exactly as
/// `ActiveScaleMenu::selectEncoderAction`'s clamp to `LAST_PRESET_SCALE` (`active_scales.cpp:104`)
/// says.
constexpr size_t kScaleRows = NUM_PRESET_SCALES;
static_assert(kScaleRows == static_cast<size_t>(LAST_PRESET_SCALE) + 1,
              "the row count must be the closed range legacy's clamp allows, or the last scale is "
              "unreachable (too few) or a non-preset scalelike leaks in (too many)");

size_t depth_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.depth : 0;
}

/// @brief The node on screen, so the item can draw its own title and name itself as the write's
///        target. The helper `dynamic.cpp` and `settings.cpp` keep.
const Node* focused_of(const MenuState& state) {
	return state.depth < kMenuDepth ? state.path[state.depth] : nullptr;
}

} // namespace

bool ActiveScalesItem::is_disabled(const MenuContext& ctx, size_t index) const {
	// `ActiveScaleMenu::isDisabled` (`gui/menu_item/active_scales.cpp:114-124`). The `else` arm's
	// `return false` is legacy's answer with no song loaded, and it is reproduced rather than
	// asserted away: the settings root resolves a context with a song, but the item must not
	// dereference a null one if it is ever opened without.
	if (index >= kScaleRows) {
		return false;
	}
	if (set_ == ScaleSet::Default) {
		return FlashStorage::defaultDisabledPresetScales[index];
	}
	if (ctx.song != nullptr) {
		return ctx.song->disabledPresetScales[index];
	}
	return false;
}

void ActiveScalesItem::set_disabled(const MenuContext& ctx, size_t index, bool value) const {
	// `ActiveScaleMenu::setDisabled` (`active_scales.cpp:126-133`), whose `else if (currentSong)`
	// makes a null song a silent no-op. `ctx.song` stands in for `currentSong` -- the substitution
	// `context.h` makes everywhere (task-3 ruling 3).
	if (index >= kScaleRows) {
		return;
	}
	if (set_ == ScaleSet::Default) {
		FlashStorage::defaultDisabledPresetScales[index] = value;
	}
	else if (ctx.song != nullptr) {
		ctx.song->disabledPresetScales[index] = value;
	}
}

void ActiveScalesItem::render(deluge::ui::oled::OledFrame& f, const MenuContext& ctx, MenuState& state) const {
	// `MenuItem::renderOLED` (`gui/menu_item/menu_item.cpp:37-41`) draws the screen title and then
	// calls `drawPixelsForOled`; `ActiveScaleMenu` overrides only the second, so the title is legacy's
	// too. (`drawName()` is overridden to nothing, `active_scales.cpp:25-26`, which is the 7SEG path.)
	const Node* node = focused_of(state);
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::title(f, node != nullptr ? deluge_profile().text_or_digits(node->title, scratch)
	                                           : std::string_view{});

	// `drawSubmenuItemsForOled` (`active_scales.cpp:62-99`): one row per scale, the name clipped
	// short of the box, and a checked or unchecked box at the right. `list` owns the window, the
	// highlight and the focused row's side scroller -- see `scales.h` for why that is the same
	// renderer and not merely a similar one.
	deluge::ui::oled::ListRow rows[kScaleRows];
	for (size_t i = 0; i < kScaleRows; i++) {
		// `scalelikeNames` holds `char const*` with static storage (`model/scale/preset_scales.h:97`),
		// so the view outlives this frame.
		rows[i].label = std::string_view{scalelikeNames[i]};
		rows[i].icon =
		    is_disabled(ctx, i) ? &deluge::gfx::oled::icons::uncheckedBox : &deluge::gfx::oled::icons::checkedBox;
		rows[i].selectable = true;
	}

	const size_t d = depth_of(state);
	state.render_resp =
	    deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), state.list[d],
	                           std::span<const deluge::ui::oled::ListRow>{rows, kScaleRows}, state.scroll[d]);

	if (!state.render_resp.entered) {
		return;
	}

	// `selectButtonPress` (`active_scales.cpp:108-112`): flip the bit and return `NO_NAVIGATION`.
	// The flip itself is `commit()`'s -- a render may not write the model -- and clearing `entered`
	// is how this item says it consumed the press, so `after_render`'s leaf arm does not ascend out
	// of a page legacy stays on. Same two moves as `PerformEditorItem` (`nodes/actions.cpp`).
	state.value_changed = true;
	state.value_node = node;
	state.render_resp.entered = false;
}

void ActiveScalesItem::commit(const MenuContext& ctx, MenuState& state) const {
	// The cursor is the row, and it cannot have moved between the render that set `value_changed`
	// and this call: `after_render` runs immediately after, with no input in between.
	const size_t d = depth_of(state);
	const int32_t cursor = state.list[d].cursor;
	if (cursor < 0 || static_cast<size_t>(cursor) >= kScaleRows) {
		return;
	}
	const size_t index = static_cast<size_t>(cursor);
	set_disabled(ctx, index, !is_disabled(ctx, index));
}

deluge::ui::shell::Result ActiveScalesItem::handle(const deluge::ui::shell::UiEvent& /*event*/,
                                                   const deluge::ui::shell::UiContext& /*ui_ctx*/,
                                                   const MenuContext& /*ctx*/, MenuState& /*state*/) const {
	// `list` claims the turn and the select during the render, as every other list page here does.
	// Nothing left to consume.
	return {};
}

} // namespace deluge::scenes::menu::nodes
