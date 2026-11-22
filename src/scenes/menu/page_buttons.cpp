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
/// @brief The horizontal page's six buttons. See `page_buttons.h`.

#include "scenes/menu/page_buttons.h"

#include "scenes/menu/bindings.h"

#include "base/notify/notice_queue.h"

#include "ui/widgets/oled/columns.h"

#include <span>

namespace deluge::scenes::menu {

using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using namespace detail;

namespace {

/// @brief Which horizontal-page column button @p b is -- legacy's `select_map` (`HorizontalMenu::buttonAction` in
///        `origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`), or -1 for every other button.
int32_t instrument_button_index(ButtonId b) {
	switch (b) {
	case ButtonId::Synth:
		return 0;
	case ButtonId::Kit:
		return 1;
	case ButtonId::Midi:
		return 2;
	case ButtonId::Cv:
		return 3;
	default:
		return -1;
	}
}

} // namespace

std::optional<Result> handle_page_button(MenuCore core, MenuWalk& walk, ValueSession& session, const UiEvent& event,
                                         const UiContext& ui, MenuContext::Root chain_root) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	// SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE on a horizontal page -- `HorizontalMenu::buttonAction`
	// (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`). Checked before the shortcut-pad jump: these six
	// are never shortcut-pad buttons, but placing this ahead keeps every button arm together and
	// matches legacy's own precedence (`buttonAction` is `MenuItem`'s override, reached before
	// `SoundEditor::buttonAction`'s own switch ever sees SELECT/BACK/etc).
	if (event.kind == UiEvent::Kind::Button && event.on && on_horizontal_page(ctx, st)) {
		const int32_t button = instrument_button_index(event.button); // Synth 0, Kit 1, Midi 2, Cv 3, else -1
		const int32_t page_dir = event.button == ButtonId::CrossScreen ? 1 : event.button == ButtonId::Scale ? -1 : 0;
		if (button >= 0 || page_dir != 0) {
			// One 140 ms debounce shared by all six buttons, one shared timestamp updated
			// only on a press this arm accepts (`HorizontalMenu::buttonAction`).
			if (ui.now_ms - st.nav_press_ms <= kNavButtonDebounceMs) {
				Result r;
				r.handled = true;
				return r;
			}
			st.nav_press_ms = ui.now_ms;

			const Node* node = focused(st);
			const Node* kids[kMaxMenuColumns];
			deluge::ui::widgets::oled::Column cols[kMaxMenuColumns]{};
			NodeText label_scratch[kMaxMenuColumns];
			const size_t n = build_column_list(ctx, *node, std::span<const Node*>{kids, kMaxMenuColumns},
			                                   std::span<deluge::ui::widgets::oled::Column>{cols, kMaxMenuColumns},
			                                   std::span<NodeText>{label_scratch, kMaxMenuColumns});
			const int32_t focused_col =
			    n == 0 ? 0 : clamp_to(st.columns[st.depth].focused, 0, static_cast<int32_t>(n) - 1);

			Result r;
			r.handled = true;
			bool repaint = false;

			if (page_dir != 0) {
				// SHIFT + CROSS_SCREEN/SCALE at depth 0 switches the chained menu (`switchHorizontalMenu`);
				// otherwise, and at depth > 0 where there is no chain, `switchVisiblePage`.
				if (st.depth == 0 && ui.shift) {
					repaint = walk.switch_chained_page(core, session, page_dir, chain_root);
				}
				else if (n > 0) {
					const int32_t target = deluge::ui::widgets::oled::page_switch(
					    std::span<const deluge::ui::widgets::oled::Column>{cols, n}, focused_col, page_dir);
					if (target != focused_col) {
						st.columns[st.depth].focused = static_cast<int8_t>(target);
						// `switchVisiblePage` runs `initializeItem(*current_item_)` on the newly
						// landed item and discards the result entirely -- not even
						// `MUST_SELECT_RANGE` is acted on here, only the allocation side effect a
						// range-specific item's permission check performs matters.
						(void)ensure_item_first_range(*kids[target], ctx);
						// `switchVisiblePage` cancels the notification unconditionally once past its
						// own `totalPages <= 1` guard -- `page_switch` returning a
						// different column already implies that guard passed.
						deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelNotification);
						repaint = true;
					}
				}
			}
			else if (n > 0) {
				const int32_t target = deluge::ui::widgets::oled::column_under_button(
				    std::span<const deluge::ui::widgets::oled::Column>{cols, n}, focused_col, button);
				// `layout == FIXED && !isItemRelevant(item)` (`HorizontalMenu::handleInstrumentButtonPress`): a
				// disabled column under the button does nothing at all, not even a focus change.
				if (target >= 0 && cols[target].relevant) {
					if (target == focused_col) {
						// `HorizontalMenu::handleItemAction`: the already-focused column's own press.
						const Node& col_node = *kids[target];
						if (col_node.kind == Kind::Submenu || col_node.allow_session_from_horizontal) {
							// Both answer true to `!isSubmenu() && !allowToBeginSessionFromHorizontalMenu()`'s
							// negation, so legacy takes the same "enter" branch for either: `enterSubmenu` after
							// the range check, with no chain redirect.
							walk.enter_horizontal_item(core, session, col_node);
						}
						else {
							// `MenuItem::selectButtonPress`'s overrides any other ported column kind reaches are
							// `Toggle`'s flip (`origin/main:src/deluge/gui/menu_item/toggle.h`) and `Selection`'s
							// (`origin/main:src/deluge/gui/menu_item/selection.h`, when `isToggle()`) -- see
							// `Node::selection_is_toggle`'s own doc for the three live examples; every other kind's
							// base is a no-op. No `select_chain` here -- legacy's `handleItemAction` calls
							// `selectButtonPress()` directly, never `enterSubmenu`, for this arm.
							if (col_node.kind == Kind::Toggle || col_node.selection_is_toggle) {
								// The column reads its value live, so a refused flip already draws the stored one.
								(void)bindings::write(col_node, ctx, bindings::read(col_node, ctx) == 0 ? 1 : 0);
							}
							notify_column(ctx, col_node);
						}
						repaint = true;
					}
					else {
						// `handleInstrumentButtonPress`'s new-focus tail: `current_item_`
						// is assigned before the range check, so the focus below happens whether or
						// not the picker ends up opening over it.
						st.columns[st.depth].focused = static_cast<int8_t>(target);
						// A refused first range (`NO`) still shows the column's notification, after the error.
						if (walk.enter_range_picker_if_needed(core, session, *kids[target],
						                                      /*suppress_when_allowed=*/true)
						    != RangeGate::Picker) {
							notify_column(ctx, *kids[target]);
						}
						repaint = true;
					}
				}
			}

			if (repaint) {
				(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
			}
			return r;
		}
	}
	return std::nullopt;
}

void page_button_leds(const MenuContext& ctx, const MenuState& st, deluge::ui::widgets::core::LedFrame& f) {
	constexpr deluge::ui::widgets::core::LedState kOff{deluge::ui::widgets::core::LedState::Kind::Off, 0, 0};
	constexpr deluge::ui::widgets::core::LedState kOn{deluge::ui::widgets::core::LedState::Kind::On, 0, 0};
	// SYNTH/KIT/MIDI/CV: `HorizontalMenu::updateSelectedMenuItemLED`
	// (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`). CROSS_SCREEN/SCALE: `renderOLED`'s own two-line LED
	// update. Both fresh every tick, gated on nothing -- an `LedFrame` is the whole LED state, redeclared each time,
	// the same as LEARN below. Off on a vertical page or with nothing focused, matching legacy's own `endSession`
	// extinguishing all six when a horizontal page is left (`HorizontalMenu::endSession`).
	bool synth = false;
	bool kit = false;
	bool midi = false;
	bool cv = false;
	bool cross_screen = false;
	bool scale = false;
	if (on_horizontal_page(ctx, st)) {
		const Node* node = focused(st);
		const Node* kids[kMaxMenuColumns];
		deluge::ui::widgets::oled::Column cols[kMaxMenuColumns]{};
		NodeText label_scratch[kMaxMenuColumns];
		const size_t n = build_column_list(ctx, *node, std::span<const Node*>{kids, kMaxMenuColumns},
		                                   std::span<deluge::ui::widgets::oled::Column>{cols, kMaxMenuColumns},
		                                   std::span<NodeText>{label_scratch, kMaxMenuColumns});
		const int32_t focused_col = n == 0 ? 0 : clamp_to(st.columns[st.depth].focused, 0, static_cast<int32_t>(n) - 1);
		const std::span<const deluge::ui::widgets::oled::Column> span{cols, n};

		// `column_under_button(span, focused_col, b) == focused_col` is exactly
		// `updateSelectedMenuItemLED`'s `i >= start_column && i < end_column` test: both name "does
		// button b's column cover the focused one", the first by re-deriving it from the same page
		// window, the second by accumulating slots up to the match.
		bool lit[4] = {};
		for (int32_t b = 0; b < 4; b++) {
			lit[b] = deluge::ui::widgets::oled::column_under_button(span, focused_col, b) == focused_col;
		}
		// `page_items.size() > 1 || page_items[0]->isSubmenu() || page_items[0]->getOccupiedSlots() <
		// 4`: legacy suppresses all four only for a *single*, *non-submenu* column spanning the
		// whole page. All four buttons landing on `focused_col` is exactly "the page holds one
		// column and it covers every slot" -- the only way every button can resolve to the same
		// index -- so this reproduces the OR as its negation without walking the page twice.
		if (lit[0] && lit[1] && lit[2] && lit[3] && n > 0 && !cols[focused_col].is_submenu) {
			lit[0] = lit[1] = lit[2] = lit[3] = false;
		}
		synth = lit[0];
		kit = lit[1];
		midi = lit[2];
		cv = lit[3];
		const bool has_pages = deluge::ui::widgets::oled::paging_of(span, focused_col).total_pages > 1;
		cross_screen = has_pages;
		scale = has_pages;
	}
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Synth), synth ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Kit), kit ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Midi), midi ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Cv), cv ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::CrossScreen), cross_screen ? kOn : kOff);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Scale), scale ? kOn : kOff);
}

} // namespace deluge::scenes::menu
