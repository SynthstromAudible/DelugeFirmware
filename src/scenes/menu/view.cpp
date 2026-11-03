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
/// @brief The menu's OLED rendering. See `view.h`.

#include "scenes/menu/view.h"

#include "scenes/menu/walk.h"

#include "scenes/menu/bindings.h"

#include "model/song/song.h" // Song::getInputTickMagnitude() -- Kind::SyncLevel's value text
#include "ui/oled/list.h"
#include "ui/oled/text.h"

#include "gui/l10n/strings.h"
#include "modulation/sync.h"
#include "ui/core/metrics.h"
#include "ui/oled/columns.h"
#include "ui/oled/values.h"

#include <algorithm>
#include <span>

namespace deluge::scenes::menu {

using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::TextScratch;
using namespace detail;

void MenuView::render(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node_ref) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	const Node* node = &node_ref;
	st.tick.render_resp = {};
	st.tick.column_press = false;
	st.tick.select_shift = f.shift();

	// **The depth test precedes the read.** At depth 0 `owns` leaves Back to `handle()`, which pops the scene --
	// legacy's back at the root closing the editor (`SoundEditor::buttonAction`'s BACK arm -> `goUpOneLevel()`, whose
	// depth-0 arm is `exitCompletely()`) -- so the render must not read it there: the read IS the claim.
	if (st.depth > 0) {
		st.tick.back_claimed = f.back_pressed();
	}

	switch (node->kind) {
	case Kind::Submenu:
		if (is_horizontal(ctx, *node)) {
			render_horizontal(core, f, *node);
		}
		else {
			render_vertical(core, f, *node);
		}
		break;
	case Kind::Int:
		render_leaf_int(core, f, *node);
		break;
	case Kind::Decimal:
		// A `DecimalWithoutScrolling` names its value and edits a step at a time, as an integer screen does.
		if (node->bind.leaf_text != nullptr) {
			render_leaf_int(core, f, *node);
		}
		else {
			render_leaf_decimal(core, f, *node);
		}
		break;
	case Kind::Enum:
		render_leaf_enum(core, f, *node);
		break;
	case Kind::Toggle:
		render_leaf_toggle(core, f, *node);
		break;
	case Kind::SyncLevel:
		render_leaf_sync_level(core, f, *node);
		break;
	case Kind::Range:
		render_leaf_range(core, f, *node);
		break;
	case Kind::Custom:
		if (node->custom != nullptr) {
			node->custom->render(f, ctx, st);
		}
		break;
	default: {
		// Every `Kind` `node.h` declares has an arm above. Nothing in the current tree reaches this
		// arm; it stays only so a `Kind` a later plan adds still draws its title rather than
		// nothing at all until that plan writes its own interpreter.
		NodeText scratch;
		deluge::ui::oled::title(f, display_text(*node, ValueTextUse::Title, ctx, scratch));
		break;
	}
	}
}

void MenuView::render_vertical(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	NodeText title_scratch;
	deluge::ui::oled::title(f, display_text(node, ValueTextUse::Title, ctx, title_scratch));

	const Node* kids[kMaxMenuChildren];
	const size_t n = visible_children(ctx, node, kids);

	deluge::ui::oled::ListRow rows[kMaxMenuChildren];
	NodeText label_scratch[kMaxMenuChildren];
	for (size_t i = 0; i < n; i++) {
		rows[i].label = display_text(*kids[i], ValueTextUse::Name, ctx, label_scratch[i]);
		rows[i].icon = row_icon(*kids[i], ctx);
		rows[i].selectable = true;
	}

	// `menu_rect(f)` is the legacy-reproducing rect (`ui/oled/list.h:33`) -- never a hand-computed
	// one. `list` clamps the cursor, walks it with `f.turn()`, and reports `entered` on a select
	// press, claiming both for the tick.
	st.tick.render_resp =
	    deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), st.list[st.depth],
	                           std::span<const deluge::ui::oled::ListRow>{rows, n}, st.scroll[st.depth]);
}

void MenuView::render_horizontal(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	const Node* kids[kMaxMenuColumns];
	deluge::ui::oled::Column cols[kMaxMenuColumns]{};
	NodeText label_scratch[kMaxMenuColumns];
	const size_t n = build_column_list(ctx, node, std::span<const Node*>{kids, kMaxMenuColumns},
	                                   std::span<deluge::ui::oled::Column>{cols, kMaxMenuColumns},
	                                   std::span<NodeText>{label_scratch, kMaxMenuColumns});

	// `HorizontalMenu::selectEncoderAction` (`gui/menu_item/horizontal_menu.cpp:309-326`): the select
	// turn edits the focused column and never moves focus. A horizontal menu is itself a submenu, so
	// `SoundEditor::selectEncoderAction` hands it the raw offset (`gui/ui/sound_editor.cpp:1122`) --
	// no SHIFT multiplier here -- and a submenu column is left alone (`:315-318`). The edit is the
	// offset times `calcNextKnobSpeed` (`:326`, `:630-633`, `kHorizontalMenuAcceleration`), truncated
	// to an int as legacy's `int32_t` parameter does. The turn is read (and claimed) BEFORE `columns`
	// draws the page, so a detent edits the column the user was looking at; `columns` itself no
	// longer touches the turn at all (`ui/oled/columns.cpp`'s own note).
	const int32_t focus = n == 0 ? 0 : clamp_to(st.columns[st.depth].focused, 0, static_cast<int32_t>(n) - 1);
	const int8_t turned = f.turn();
	f.claim_turn();
	if (n != 0 && turned != 0 && kids[focus]->kind != Kind::Submenu) {
		const Node& target = *kids[focus];
		// `Binding::permitted`: legacy's
		// `selectEncoderActionIsPermitted()` check, consulted *before* the edit and *before* the
		// notification (`horizontal_menu.cpp:321-324`, ahead of `:326-328`). A refusal stages
		// nothing and raises no notification here -- the hook raises its own popup, exactly as
		// legacy's side effect does.
		if (target.bind.permitted == nullptr || target.bind.permitted(ctx)) {
			auto detents = static_cast<int32_t>(
			    turned
			    * horizontal_acceleration_.advance(turned, static_cast<double>(f.now_ms()) / 1000.0,
			                                       deluge::ui::kHorizontalMenuAcceleration));
			if (target.bind.reverse_in_horizontal != nullptr && target.bind.reverse_in_horizontal(ctx)) {
				detents = -detents;
			}
			if (target.bind.adjust != nullptr) {
				// `Binding::adjust`: accumulate the turn's detents rather than
				// computing `moved` -- an offset write (`instrumentClipView.adjustVelocity` and
				// its siblings) has no absolute value to stage, so there is nothing to put in
				// `edit_int`/`enum_index` here. The column below reads `bindings::read` for this
				// node instead of any edit storage, for the same reason.
				st.tick.edit_offset += detents;
			}
			else {
				const IntRange range = bindings::range(target, ctx);
				// The toolkit's own guard: a zero `step` would give a dead select knob
				// (`ui/oled/values.cpp:38-40`'s `coarse_step`).
				int32_t step = range.step != 0 ? range.step : 1;
				// `getNumberEditSize` on a horizontal page (`decimal.cpp:49-54`): a Decimal moves 1.00 a detent, 0.01
				// with SELECT or SHIFT held; a node's own `HorizontalStep` overrides either.
				const HorizontalStep& hs = target.bind.horizontal_step;
				if (target.kind == Kind::Decimal || hs.coarse != 0) {
					const int32_t coarse = hs.coarse != 0 ? hs.coarse : (target.kind == Kind::Decimal ? 100 : step);
					const int32_t fine = hs.fine != 0 ? hs.fine : 1;
					step = (f.select_held() || f.shift()) ? fine : coarse;
				}
				int32_t current = bindings::read(target, ctx);
				const std::optional<int32_t> turned_to =
				    target.bind.turn != nullptr ? target.bind.turn(ctx, current, detents) : std::nullopt;
				if (hs.restart_from_negative && detents > 0 && current < 0) {
					current = -step;
				}
				int32_t moved = current + detents * step;
				if (target.bind.wrap_in_horizontal && range.max >= range.min) {
					const int32_t span = range.max - range.min + 1;
					moved = range.min + (((moved - range.min) % span) + span) % span;
				}
				moved = clamp_to(turned_to.value_or(moved), range.min, range.max);
				// Set on every permitted turn, clamp or not: legacy calls `child->selectEncoderAction(...)`
				// and `displayNotification(child)` unconditionally once it has decided the turn is permitted
				// (`selectEncoderActionIsPermitted()`, `:321`) -- it never re-reads the value first to
				// see whether the clamp actually moved it (`:326-328`). Gating this on `moved != current`
				// dropped the notification on a turn already at an endpoint; `commit_value` writing the same
				// value back at a clamp is harmless, exactly as legacy's own re-write is.
				switch (target.kind) {
				case Kind::Enum:
					st.enum_index = moved;
					break;
				case Kind::Toggle:
					st.edit_bool = moved != 0;
					break;
				default:
					st.edit_int = moved;
					break;
				}
			}
			st.tick.value_changed = true;
			st.tick.value_node = &target;
		}
	}

	// Everything a `Column` points at has to outlive the `columns` call.
	std::span<ColumnSlot> slots{column_slots_};
	for (size_t i = 0; i < n; i++) {
		const Node& kid = *kids[i];
		// A `Binding::adjust` node stages no absolute value (see the edit block above) -- only
		// `bindings::read` is ever meaningful for it, so its column always re-reads rather than
		// consulting `st.tick.value_node`/`edit_int`. That leaves one frame of lag before the commit's
		// write is visible here, because renders never write the model; `node.h`'s `AdjustFn` doc
		// names this rather than working around it.
		const bool has_staged_edit = kid.bind.adjust == nullptr && st.tick.value_node == &kid;
		int32_t value = bindings::read(kid, ctx);
		if (has_staged_edit) {
			value = kid.kind == Kind::Enum     ? st.enum_index
			        : kid.kind == Kind::Toggle ? int32_t{st.edit_bool}
			                                   : st.edit_int;
		}
		present_column(kid, ctx, value, slots[i], cols[i]);
	}

	NodeText title_scratch;
	// `f.bounds()`, not `menu_rect(f)`: this widget owns the whole panel including its own title
	// band (`ui/oled/columns.h:127`).
	// A member page drawn whole (`Node::page_draw`): legacy's `renderMenuItems` overrides, which number the focused
	// item among the member's own.
	struct PageDrawCall {
		const Node* member;
		const MenuContext* ctx;
		int32_t focus;
	};
	PageDrawCall call{};
	deluge::ui::oled::PageRender page_render{};
	if (n > 0) {
		const int32_t focus = clamp_to(st.columns[st.depth].focused, 0, static_cast<int32_t>(n) - 1);
		const Node* member = bindings::group_member_of(node, ctx, static_cast<size_t>(focus));
		if (member != nullptr && member->page_draw != nullptr) {
			int32_t first = focus;
			while (first > 0 && bindings::group_member_of(node, ctx, static_cast<size_t>(first - 1)) == member) {
				first--;
			}
			call = PageDrawCall{.member = member, .ctx = &ctx, .focus = focus - first};
			page_render = {.draw =
			                   [](deluge::ui::oled::OledFrame& frame, int32_t, void* ctx) {
				                   const auto& c = *static_cast<const PageDrawCall*>(ctx);
				                   deluge::ui::oled::OledFrame panel = deluge::ui::oled::panel_frame(frame);
				                   c.member->page_draw(panel, *c.ctx, c.focus);
			                   },
			               .ctx = &call};
		}
	}
	st.tick.render_resp = deluge::ui::oled::columns(f, f.bounds(), st.columns[st.depth],
	                                                std::span<const deluge::ui::oled::Column>{cols, n},
	                                                page_title(ctx, st, title_scratch), page_render);

	// `columns` claims the press only for a submenu column (`ui/oled/columns.cpp:451-455`), so a
	// press on a *value* column reaches nobody. `after_render` answers it as `Submenu::selectButtonPress`
	// forwarding to the focused item does (`gui/menu_item/submenu.cpp:187-194`): enter its
	// `select_chain` target, or `ascend()` -- see `MenuTick::column_press`'s own doc for why that
	// is not `HorizontalMenu::handleItemAction`. The read is on the same frame instance `columns`
	// was handed, so the claim is idempotent (`ui/core/frame_context.h`, rule 2) and never trips
	// `double_claim`.
	const int32_t settled = n == 0 ? -1 : clamp_to(st.columns[st.depth].focused, 0, static_cast<int32_t>(n) - 1);
	if (settled >= 0 && kids[settled]->kind != Kind::Submenu) {
		st.tick.column_press = f.select_pressed();
	}
}

void MenuView::render_leaf_int(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	NodeText scratch;
	deluge::ui::oled::title(f, display_text(node, ValueTextUse::Title, ctx, scratch));

	// `f.bounds()`, NOT `menu_rect(f)`: `int_value` places its digits at a rect-relative
	// `kValueY`, calibrated (per its own doc comment) for `rect == f.bounds()` -- exactly the
	// same reason `render_leaf_decimal` below takes `f.bounds()` rather than `menu_rect(f)`.
	// Passing `menu_rect(f)` here would add `Style::menu_base_y` (14px on the 48-row panel) on top of
	// `kValueY`'s own offset and push the huge digit glyph low enough to clip off the panel's
	// bottom edge.
	// A param is legacy's `IntegerContinuous` (`patched_param::Integer`, `UnpatchedParam`): the digits over a value
	// bar.
	deluge::ui::oled::ValueStyle style = node.bind.style;
	style.continuous = node.bind.param.valid();
	struct LabelCall {
		const Node* node;
		const MenuContext* ctx;
	};
	const LabelCall call{&node, &ctx};
	style.hook_ctx = &call;
	if (node.bind.turn != nullptr) {
		style.turn = [](int32_t value, int32_t offset, const void* c) -> std::optional<int32_t> {
			const auto& lc = *static_cast<const LabelCall*>(c);
			return lc.node->bind.turn(*lc.ctx, value, offset);
		};
	}
	if (node.bind.leaf_text != nullptr) {
		style.label = [](int32_t value, const void* c, std::span<char> out) -> std::string_view {
			const auto& lc = *static_cast<const LabelCall*>(c);
			const ValueText vt = lc.node->bind.leaf_text(*lc.ctx, value, ValueTextUse::Column, out);
			return vt.fallback ? std::string_view{} : std::string_view{out.data(), std::min(vt.length, out.size())};
		};
	}
	const deluge::ui::Response r =
	    deluge::ui::oled::int_value(f, f.bounds(), st.edit_int, bindings::range(node, ctx), style, /*focused=*/true);
	st.tick.render_resp = r;
	if (r.changed) {
		st.tick.value_changed = true;
		st.tick.value_node = &node;
	}
	// `int_value` owns the select encoder -- legacy edits the value with it on a number screen
	// (`SoundEditor::selectEncoderAction`, `sound_editor.cpp:1122`), and so does this port
	// (`ui/oled/values.cpp`'s `select_delta`). The select *press* that leaves the item
	// (`MenuItem::selectButtonPress` returning nullptr -> `goUpOneLevel`) is still this scene's to
	// claim -- `int_value` never reads `select_pressed()`.
	if (f.select_pressed()) {
		st.tick.render_resp.entered = true;
	}
	// This claim is idempotent (`ui/core/frame_context.h` rule 2): `int_value` already claimed the
	// turn above while editing. Kept so the ownership reads here too.
	f.claim_turn();
}

void MenuView::render_leaf_range(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	NodeText scratch;
	deluge::ui::oled::title(f, display_text(node, ValueTextUse::Title, ctx, scratch));

	const RangeSpec* spec = node.bind.range_spec;
	if (spec == nullptr || spec->format == nullptr) {
		// `validate_tree` reports this (`Issue::Kind::RangeWithoutSpec`); drawing the title and
		// nothing else is the same fail-visible choice `render_oled`'s null-custom arm makes.
		return;
	}

	// `f.bounds()`, NOT `menu_rect(f)`, for `render_leaf_int`'s reason: `range_value` places its
	// digits at a rect-relative `kValueY` calibrated for the whole panel.
	const deluge::ui::Response r =
	    deluge::ui::oled::range_value(f, f.bounds(), st.edit_int, st.edit_upper, bindings::range(node, ctx),
	                                  st.edit_edge, spec->format, spec->clamp, /*focused=*/true);
	st.tick.render_resp = r;
	if (r.changed) {
		st.tick.value_changed = true;
		st.tick.value_node = &node;
	}
	// `range_value` owns the select and horizontal encoders, not the gold knobs (`values.h`'s
	// note on the two `selectEncoderAction`s it ports); the select press that leaves the item is
	// this scene's, exactly as on a number screen.
	if (f.select_pressed()) {
		st.tick.render_resp.entered = true;
	}
	f.claim_turn(); // idempotent (`ui/core/frame_context.h` rule 2): `range_value` already claimed it above.
}

void MenuView::render_leaf_decimal(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	NodeText scratch;
	deluge::ui::oled::title(f, display_text(node, ValueTextUse::Title, ctx, scratch));

	// `f.bounds()`, NOT `menu_rect(f)`: `decimal_value` draws at a canvas-absolute y and honours
	// the rect horizontally only (`ui/oled/values.h:111`), and its goldens were recorded that way.
	const deluge::ui::Response r =
	    deluge::ui::oled::decimal_value(f, f.bounds(), st.edit_int, bindings::range(node, ctx),
	                                    node.bind.decimal_places, st.decimal_pos, st.cursor_blink,
	                                    /*focused=*/true);
	st.tick.render_resp = r;
	if (r.changed) {
		st.tick.value_changed = true;
		st.tick.value_node = &node;
	}
	if (f.select_pressed()) {
		st.tick.render_resp.entered = true;
	}
	// Idempotent, as `render_leaf_int`: `decimal_value` already claimed the turn above while editing.
	f.claim_turn();
}

void MenuView::render_leaf_enum(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	NodeText scratch;
	deluge::ui::oled::title(f, display_text(node, ValueTextUse::Title, ctx, scratch));

	std::string_view names_buf[kMaxDynamicNames];
	TextScratch names_scratch[kMaxDynamicNames];
	const size_t n = bindings::names(node, ctx, names_buf, names_scratch);

	// `enum_value` takes a `ListState` AND a `ScrollText` of its own and has no `focused`
	// parameter -- it owns the turn and the select itself (`ui/oled/values.h:190-191`).
	const deluge::ui::Response r =
	    deluge::ui::oled::enum_value(f, deluge::ui::oled::menu_rect(f), st.enum_index,
	                                 std::span<const std::string_view>{names_buf, n}, st.enum_list, st.enum_scroll,
	                                 /*wrap=*/false);
	st.tick.render_resp = r;
	if (r.changed) {
		st.tick.value_changed = true;
		st.tick.value_node = &node;
	}
}

void MenuView::render_leaf_toggle(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	NodeText scratch;
	deluge::ui::oled::title(f, display_text(node, ValueTextUse::Title, ctx, scratch));

	TextScratch off_scratch;
	TextScratch on_scratch;
	const std::string_view off = text_of(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_OFF), off_scratch);
	const std::string_view on = text_of(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ON), on_scratch);

	// A `bool&`, no `focused`; a select press reports `entered` and leaves `changed` false
	// (`ui/oled/values.h:202-206`).
	const deluge::ui::Response r =
	    deluge::ui::oled::toggle(f, deluge::ui::oled::menu_rect(f), st.edit_bool, off, on, st.scroll[st.depth]);
	st.tick.render_resp = r;
	if (r.changed) {
		st.tick.value_changed = true;
		st.tick.value_node = &node;
	}
}

void MenuView::render_leaf_sync_level(MenuCore core, deluge::ui::oled::OledFrame& f, const Node& node) {
	MenuContext& ctx = core.ctx;
	MenuState& st = core.st;
	NodeText scratch;
	deluge::ui::oled::title(f, display_text(node, ValueTextUse::Title, ctx, scratch));

	// Legacy is `SyncLevel::drawPixelsForOled` (`gui/menu_item/sync_level.cpp:30-38`). `SyncLevel`
	// derives from `Enumeration`, not `Integer` (`sync_level.h:27`), so -- unlike `render_leaf_int`
	// just above -- the SELECT ENCODER moves the value here, clamped rather than wrapped
	// (`Enumeration::selectEncoderAction`, `enumeration.cpp:19-31`, `wrapAround()` unoverridden by
	// `SyncLevel`). The gold knobs are left unclaimed, same as legacy leaves them alone for any
	// `Enumeration`.
	const IntRange r = bindings::range(node, ctx);
	if (const int8_t d = f.turn(); d != 0) {
		const int32_t next = std::clamp<int32_t>(st.edit_int + d, r.min, r.max);
		if (next != st.edit_int) {
			st.edit_int = next;
			st.tick.render_resp.changed = true;
			st.tick.value_changed = true;
			st.tick.value_node = &node;
		}
	}

	// `f.bounds()`, NOT `menu_rect(f)` -- same reasoning as `render_leaf_int` above (`menu_rect(f)`
	// would add `Style::menu_base_y` on top of the baseline's own offset and clip the string off
	// the panel's bottom edge). But the font is `MetricBold13`, NOT `f.style.value_font`/`big_font`
	// (both `MetricBold20`): legacy draws this one in the smaller "Big" face at its own baseline
	// (`sync_level.cpp:37`, `kSyncValueY`'s doc comment in `ui/core/metrics.h`), and `Style` carries
	// no field for it -- `MetricBold9` is hard-coded the same way at `ui/oled/columns.cpp:277` for
	// the same reason, a legacy font choice with no `Style` counterpart. Getting this wrong doesn't
	// just mismatch the pixels: a note-length string in the huge font (`kTextHugeSpacingX` = 18px
	// per character) runs off both edges of a 128px panel, which is the clipping this
	// choice avoids.
	//
	// No `f.clear(band)` here, unlike `int_value`/`decimal_value`: this scene may not call an
	// `OledFrame` drawing primitive directly (this file's own top-of-file note), and legacy's
	// `SyncLevel::drawPixelsForOled` does not clear its band either -- `Shell::render_oled` already
	// clears the whole canvas before any scene draws (`shell/shell.cpp:683`), which is what makes
	// that safe for a full-panel screen like this one.
	constexpr deluge::gfx::oled::FontId kFont = deluge::gfx::oled::FontId::MetricBold13;
	const deluge::gfx::oled::Rect band{f.bounds().x, f.bounds().y + deluge::ui::kSyncValueY, f.bounds().w,
	                                   deluge::ui::oled::text_height_for(kFont)};

	// Centred text, not digits: legacy draws the literal "OFF" at zero, else `syncValueToString`
	// against the song's *input* tick magnitude (`sync_level.cpp:26-28`), never the raw menu-option
	// integer this node's binding actually stores.
	etl::string<30> buffer;
	std::string_view text_val;
	if (st.edit_int != 0) {
		syncValueToString(static_cast<uint32_t>(st.edit_int), buffer, input_tick_magnitude(ctx));
		text_val = std::string_view{buffer.data(), buffer.size()};
	}
	else {
		TextScratch off_scratch;
		text_val = text_of(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_OFF), off_scratch);
	}
	deluge::ui::oled::text(f, band, text_val, {.font = kFont, .align = deluge::ui::oled::Align::Centre});

	// The select press leaves the item, same as every other leaf's arm.
	if (f.select_pressed()) {
		st.tick.render_resp.entered = true;
	}
}

deluge::ui::InputClaims owned_input(const MenuContext& ctx, const MenuState& st) {
	deluge::ui::InputClaims c{};
	c.back = st.depth > 0; // `render_oled` reads Back only below the root
	const Node* node = focused(st);
	if (node == nullptr) {
		return c;
	}
	switch (node->kind) {
	case Kind::Submenu:
		if (is_horizontal(ctx, *node)) {
			// `render_horizontal` reads the turn on every page; the press goes to `columns` on a relevant submenu
			// column and to the value column's own read otherwise, which leaves an irrelevant submenu column's.
			c.turn = true;
			const Node* kids[kMaxMenuColumns];
			const size_t n = visible_children(ctx, *node, std::span<const Node*>{kids, kMaxMenuColumns});
			if (n > 0) {
				const Node& kid = *kids[clamp_to(st.columns[st.depth].focused, 0, static_cast<int32_t>(n) - 1)];
				c.select = kid.kind != Kind::Submenu || bindings::relevant(kid, ctx);
			}
		}
		else {
			c.turn = c.select = true; // `list`
		}
		break;
	case Kind::Decimal:
		// A `DecimalWithoutScrolling` draws as an integer screen; `decimal_value` moves its cursor with the scroll.
		c.horizontal = node->bind.leaf_text == nullptr;
		c.turn = c.select = true;
		break;
	case Kind::Range:
		// `render_leaf_range` draws only the title for a range without a spec.
		if (node->bind.range_spec != nullptr && node->bind.range_spec->format != nullptr) {
			c.turn = c.select = c.horizontal = true;
		}
		break;
	case Kind::Int:
	case Kind::Enum:
	case Kind::Toggle:
	case Kind::SyncLevel:
		c.turn = c.select = true;
		break;
	case Kind::Custom:
		if (node->custom != nullptr) {
			const deluge::ui::InputClaims item = node->custom->owned_input(ctx, st);
			c.turn = item.turn;
			c.edit0 = item.edit0;
			c.edit1 = item.edit1;
			c.horizontal = item.horizontal;
			c.vertical = item.vertical;
			c.select = item.select;
		}
		break;
	default:
		break;
	}
	return c;
}

} // namespace deluge::scenes::menu
