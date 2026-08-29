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
/// @brief The menu interpreter. See `menu_scene.h` for what it is and which legacy routines it
///        ports.
///
/// **Where each thing happens, and why.** The toolkit's rule is that a widget draws and consumes
/// its inputs in the same call (`ui/core/frame_context.h`, rule 2: *reading is claiming*), so
/// `render_oled` is where input is observed -- but it may not touch the model. `after_render()`
/// is therefore where a `Response` becomes navigation or a `bindings::write`, and `handle()` is
/// left with only the two things a widget can never claim: a pad press (positional, never masked
/// -- rule 4) and a button nothing owned.

#include "scenes/menu/menu_scene.h"

#include "scenes/menu/bindings.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/shortcuts.h"
#include "scenes/profile.h"

#include "gfx/oled/icons.h"
#include "gui/l10n/strings.h"
#include "notify/notice_queue.h"
#include "shell/leds.h"
#include "ui/oled/columns.h"
#include "ui/oled/list.h"
#include "ui/oled/meters.h"
#include "ui/oled/text.h"
#include "ui/oled/values.h"

#include <algorithm>
#include <cstring>

namespace deluge::scenes::menu {

using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SceneState;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::TextScratch;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

/// @brief The colour a flashed shortcut pad takes.
///
/// `SoundEditor::setupShortcutBlink` passes `colour == 0` for every ordinary param
/// (`sound_editor.cpp:959`), which `PadLEDs::flashMainPad` (`hid/led/pad_leds.h:128`) turns into
/// the default flash, `flashColours[0]` (`hid/led/pad_leds.cpp:277-280`).
constexpr RGB kFlashColour{130, 120, 130};

/// @brief The back LED's blink period: legacy's `kFlashTime`, the period
///        `indicator_leds::blinkLed(IndicatorLED::BACK)` (`sound_editor.cpp:314`) runs at
///        (`hid/led/indicator_leds.cpp:108`, `definitions_cxx.hpp:159`).
constexpr uint16_t kBackBlinkMs = 110;

/// @brief Is a free-running blink lit at @p now_ms?
///
/// `shell/leds.cpp`'s `led_is_on` `Blink` arm -- `(now / period) & 1` -- written in difference
/// form against `Blink::until_ms`, which `ui/core/state.h:38` defines as the phase *anchor*
/// rather than a deadline. With the default anchor of 0 the two are the same expression; the
/// difference form additionally survives the ~49.7-day `uint32_t` wrap, exactly as
/// `RepaintGate::needs_frame` does.
///
/// @param b      The blink state (period and phase anchor).
/// @param now_ms The tick's clock.
/// @return True on the even half of the phase; true for a zero period, which has no phase to be
///         in.
bool blink_on(const deluge::ui::Blink& b, uint32_t now_ms) {
	if (b.period_ms == 0) {
		return true;
	}
	return (((now_ms - b.until_ms) / b.period_ms) & 1u) == 0;
}

/// @brief Resolve a `Node`'s l10n id to text through the one sanctioned path
///        (`shell/profile.h:156`, which already implements the digits fallback).
std::string_view text_of(uint16_t string_id, TextScratch& scratch) {
	return scenes::deluge_profile().text_or_digits(string_id, scratch);
}

/// @brief The row icon legacy's `renderSubmenuItemTypeForOled` draws for @p node
///        (`gui/menu_item/menu_item.cpp:69` for the submenu arrow; `Toggle`'s override for the
///        checkbox).
const deluge::gfx::oled::Icon* row_icon(const Node& node, const MenuContext& ctx) {
	switch (node.kind) {
	case Kind::Submenu:
		return &deluge::gfx::oled::icons::submenuArrow;
	case Kind::Toggle:
		return bindings::read(node, ctx) != 0 ? &deluge::gfx::oled::icons::checkedBox
		                                      : &deluge::gfx::oled::icons::uncheckedBox;
	default:
		return nullptr;
	}
}

/// @brief Render @p value into @p buf as decimal digits.
/// @return A view over @p buf; never longer than @p n.
std::string_view format_int(int32_t value, char* buf, size_t n) {
	if (n == 0) {
		return {};
	}
	char digits[12];
	size_t d = 0;
	const bool negative = value < 0;
	uint32_t magnitude = negative ? static_cast<uint32_t>(-static_cast<int64_t>(value)) : static_cast<uint32_t>(value);
	do {
		digits[d++] = static_cast<char>('0' + magnitude % 10);
		magnitude /= 10;
	} while (magnitude != 0 && d < sizeof(digits));
	size_t out = 0;
	if (negative && out < n) {
		buf[out++] = '-';
	}
	while (d > 0 && out < n) {
		buf[out++] = digits[--d];
	}
	return {buf, out};
}

/// @brief Render @p value into @p buf with a period @p places from the right -- the horizontal
///        menu's short form of `Decimal::getNotificationValue`.
std::string_view format_decimal(int32_t value, uint8_t places, char* buf, size_t n) {
	if (places == 0) {
		return format_int(value, buf, n);
	}
	char whole[16];
	const std::string_view digits = format_int(value < 0 ? -value : value, whole, sizeof(whole));
	size_t out = 0;
	if (value < 0 && out < n) {
		buf[out++] = '-';
	}
	// Zero-pad to at least `places + 1` digits so the period always has a whole part before it.
	const size_t pad = digits.size() > places ? 0 : places + 1 - digits.size();
	for (size_t i = 0; i < pad && out < n; i++) {
		buf[out++] = '0';
	}
	const size_t total = pad + digits.size();
	for (size_t i = 0; i < digits.size() && out < n; i++) {
		if (pad + i == total - places && out < n) {
			buf[out++] = '.';
		}
		if (out < n) {
			buf[out++] = digits[i];
		}
	}
	return {buf, out};
}

/// @brief `norm` for a `MeterColumn`: where @p value sits in `[min, max]`.
float normalised(int32_t value, IntRange range) {
	const int32_t span = range.max - range.min;
	if (span == 0) {
		return 0.0f;
	}
	return static_cast<float>(value - range.min) / static_cast<float>(span);
}

int32_t clamp_to(int32_t v, int32_t lo, int32_t hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

/// @brief Does this kind draw a value screen rather than a list of children?
bool is_leaf(Kind k) {
	return k != Kind::Submenu;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Lifecycle

void MenuScene::open(const Node& root, const MenuContext& ctx) {
	ctx_ = ctx;
	st_ = MenuState{};
	retarget(root);
}

SceneKind MenuScene::kind() const {
	return SceneKind::FirstApp;
}

std::span<const Node* const> MenuScene::roots() {
	// One slot per `MenuContext::Root`, all null until Tasks 6-8 write the tables. Declared now so
	// `on_enter`'s "arg 0 means the root the context chose" rule has somewhere to look, and so
	// filling it in is a change to this array alone.
	static const Node* const kRoots[static_cast<size_t>(MenuContext::Root::Count)] = {};
	return kRoots;
}

void MenuScene::retarget(const Node& node) {
	st_.path[0] = &node;
	for (uint8_t i = 1; i < kMenuDepth; i++) {
		st_.path[i] = nullptr;
	}
	st_.depth = 0;
	st_.list[0] = deluge::ui::ListState{};
	st_.columns = deluge::ui::ColumnsState{};
	st_.scroll = deluge::ui::ScrollText{};
	st_.render_resp = {};
	st_.value_changed = false;
	st_.value_node = nullptr;
	st_.back_claimed = false;
	if (is_leaf(node.kind)) {
		enter_leaf(node);
	}
}

bool MenuScene::on_enter(const SceneId& id) {
	// `SceneId::arg` is deliberately NOT part of the `SceneState` cache key (`shell/scene.h:56`),
	// so a re-entry restores the kind's last saved state and *then* lands here: this must
	// overwrite `path`/`depth` from `arg` rather than trust what `restore_state` put there.
	const Node* target = nullptr;
	if (id.arg != 0) {
		target = node_by_id(id.arg);
	}
	else {
		// Arg 0 means "the root this context chose". In E1 `roots()` is all null, so this is the
		// brief's "refuse arg == 0"; from Task 6 on it is a real fallback.
		const std::span<const Node* const> r = roots();
		const size_t i = static_cast<size_t>(ctx_.root);
		target = i < r.size() ? r[i] : nullptr;
	}
	if (target == nullptr) {
		return false;
	}
	retarget(*target);
	return true;
}

void MenuScene::on_resume() {
	// Nothing above the menu can have changed what it edits (E1 pushes nothing over it), so
	// resuming keeps the path exactly as it was. Reset only the per-tick observations, which
	// belong to a render pass that never happened.
	st_.render_resp = {};
	st_.value_changed = false;
	st_.back_claimed = false;
}

void MenuScene::on_model_changed(const deluge::notify::DirtySet& dirty) {
	// A song swap invalidates every pointer in `ctx_`. The application re-resolves the context and
	// calls `open()` again; until it does, refuse to render anything by dropping the path, which
	// is strictly safer than drawing a menu over a freed `Sound`.
	if (dirty.has(deluge::notify::Dirty::SongSwapped)) {
		st_ = MenuState{};
		ctx_ = MenuContext{};
	}
}

const Node* MenuScene::focused() const {
	return st_.depth < kMenuDepth ? st_.path[st_.depth] : nullptr;
}

ParamDescriptor MenuScene::learn_target() const {
	const Node* node = focused();
	return node != nullptr ? bindings::learn_target(*node, ctx_) : ParamDescriptor{};
}

// ---------------------------------------------------------------------------------------------
// Tree walking

size_t MenuScene::visible_children(const Node& node, std::span<const Node*> out) const {
	size_t kept = 0;
	if (node.dynamic_children != nullptr) {
		const size_t produced = std::min(node.dynamic_children(ctx_, out), out.size());
		for (size_t i = 0; i < produced; i++) {
			const Node* child = out[i];
			if (child != nullptr && (child->relevant == nullptr || child->relevant(ctx_))) {
				out[kept++] = child; // kept <= i always, so this compaction never overwrites ahead
			}
		}
		return kept;
	}
	for (const Node* child : node.children) {
		if (kept >= out.size()) {
			break;
		}
		if (child != nullptr && (child->relevant == nullptr || child->relevant(ctx_))) {
			out[kept++] = child;
		}
	}
	return kept;
}

const Node* MenuScene::child_at(const Node& node, int32_t index) const {
	const Node* kids[kMaxMenuChildren];
	const size_t n = visible_children(node, kids);
	if (index < 0 || static_cast<size_t>(index) >= n) {
		return nullptr;
	}
	return kids[index];
}

const Node* MenuScene::selected_child(const Node& node) const {
	if (node.kind != Kind::Submenu) {
		return nullptr;
	}
	return node.layout == Layout::Horizontal ? child_at(node, st_.columns.focused)
	                                         : child_at(node, st_.list[st_.depth].cursor);
}

// ---------------------------------------------------------------------------------------------
// Rendering. Draws AND consumes input (the toolkit's rule); never touches the model.

void MenuScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	// One `oled_role_by_index` entry on this board, so every other role is somebody else's panel
	// (spec E §3 rule 5). Returning before anything is read keeps the claims clean too.
	if (role != SurfaceRole::Main) {
		return;
	}
	const Node* node = focused();
	if (node == nullptr) {
		return;
	}

	st_.render_resp = {};

	// **The depth test precedes the read** (task-5 ruling 3). At depth 0 `back_pressed()` is never
	// called at all, so the input stays unclaimed and `Router::route_post_render` delivers
	// `Button{Back}` to `handle()`, which pops the scene -- legacy's back at the root closing the
	// editor (`sound_editor.cpp:418` -> `:433` `goUpOneLevel()` -> `:678` `exitCompletely()`).
	// Reading first and deciding afterwards is impossible: the read IS the claim.
	if (st_.depth > 0) {
		st_.back_claimed = f.back_pressed();
	}

	switch (node->kind) {
	case Kind::Submenu:
		if (node->layout == Layout::Horizontal) {
			render_horizontal(f, *node);
		}
		else {
			render_vertical(f, *node);
		}
		break;
	case Kind::Int:
		render_leaf_int(f, *node);
		break;
	case Kind::Decimal:
		render_leaf_decimal(f, *node);
		break;
	case Kind::Enum:
		render_leaf_enum(f, *node);
		break;
	case Kind::Toggle:
		render_leaf_toggle(f, *node);
		break;
	case Kind::Custom:
		if (node->custom != nullptr) {
			node->custom->render(f, ctx_, st_);
		}
		break;
	default: {
		// `Bipolar`, `Range`, `MultiRange`, `Colour`, `SyncLevel` and `Action` have no interpreter
		// yet -- they are E2's, and `node.h` declares them now only so the tree shape is complete.
		// Drawing the title alone is the honest placeholder: the user sees which item they are on
		// rather than the previous screen's pixels.
		TextScratch scratch;
		deluge::ui::oled::title(f, text_of(node->title, scratch));
		break;
	}
	}
}

void MenuScene::render_vertical(deluge::ui::oled::OledFrame& f, const Node& node) {
	TextScratch title_scratch;
	deluge::ui::oled::title(f, text_of(node.title, title_scratch));

	const Node* kids[kMaxMenuChildren];
	const size_t n = visible_children(node, kids);

	deluge::ui::oled::ListRow rows[kMaxMenuChildren];
	TextScratch label_scratch[kMaxMenuChildren];
	for (size_t i = 0; i < n; i++) {
		rows[i].label = text_of(kids[i]->name, label_scratch[i]);
		rows[i].icon = row_icon(*kids[i], ctx_);
		rows[i].selectable = true;
	}

	// `menu_rect(f)` is the legacy-reproducing rect (`ui/oled/list.h:33`) -- never a hand-computed
	// one. `list` clamps the cursor, walks it with `f.turn()`, and reports `entered` on a select
	// press, claiming both for the tick.
	st_.render_resp = deluge::ui::oled::list(f, deluge::ui::oled::menu_rect(f), st_.list[st_.depth],
	                                         std::span<const deluge::ui::oled::ListRow>{rows, n}, st_.scroll);
}

void MenuScene::render_horizontal(deluge::ui::oled::OledFrame& f, const Node& node) {
	const Node* kids[kMaxMenuColumns];
	const size_t n = visible_children(node, std::span<const Node*>{kids, kMaxMenuColumns});

	// The gold knobs are read BEFORE `columns` walks the focus, so a detent edits the column the
	// user was looking at rather than the one the same tick's turn moved onto. `columns` owns the
	// turn and hands its focused slot `InputMask{.turn = false}` (`ui/oled/columns.h:139`); the
	// ready-made slot renderers draw only, so the edit encoders are this scene's to read.
	const int32_t focus = n == 0 ? 0 : clamp_to(st_.columns.focused, 0, static_cast<int32_t>(n) - 1);
	const int8_t coarse = f.edit(0);
	const int8_t fine = f.edit(1);
	if (n != 0 && (coarse != 0 || fine != 0)) {
		const Node& target = *kids[focus];
		const IntRange range = bindings::range(target, ctx_);
		const int32_t moved = clamp_to(bindings::read(target, ctx_) + coarse * range.step + fine, range.min, range.max);
		if (moved != bindings::read(target, ctx_)) {
			switch (target.kind) {
			case Kind::Enum:
				st_.enum_index = moved;
				break;
			case Kind::Toggle:
				st_.edit_bool = moved != 0;
				break;
			default:
				st_.edit_int = moved;
				break;
			}
			st_.value_changed = true;
			st_.value_node = &target;
		}
	}

	// Per-column presentation. Everything a `Column` points at has to outlive the `columns` call,
	// so it all lives in this frame.
	deluge::ui::oled::Column cols[kMaxMenuColumns]{};
	deluge::ui::oled::MeterColumn meters[kMaxMenuColumns]{};
	deluge::ui::oled::TextColumn texts[kMaxMenuColumns]{};
	deluge::ui::oled::ToggleColumn toggles[kMaxMenuColumns]{};
	TextScratch label_scratch[kMaxMenuColumns];
	char value_buf[kMaxMenuColumns][16]{};
	std::string_view names_buf[kMaxDynamicNames];
	TextScratch names_scratch[kMaxDynamicNames];

	for (size_t i = 0; i < n; i++) {
		const Node& kid = *kids[i];
		cols[i].label = text_of(kid.name, label_scratch[i]);
		cols[i].slots = kid.slots;
		cols[i].show_label = true;
		cols[i].relevant = kid.relevant == nullptr || kid.relevant(ctx_);
		cols[i].is_submenu = kid.kind == Kind::Submenu;

		const int32_t value = (st_.value_node == &kid) ? (kid.kind == Kind::Enum ? st_.enum_index : st_.edit_int)
		                                               : bindings::read(kid, ctx_);

		switch (kid.kind) {
		case Kind::Submenu:
			cols[i].render = deluge::ui::oled::submenu_column;
			break;
		case Kind::Toggle:
			toggles[i].on = (st_.value_node == &kid) ? st_.edit_bool : value != 0;
			cols[i].render = deluge::ui::oled::toggle_column;
			cols[i].ctx = &toggles[i];
			break;
		case Kind::Enum: {
			const size_t count = bindings::names(kid, ctx_, names_buf, names_scratch);
			const std::string_view name =
			    count == 0 ? std::string_view{} : names_buf[static_cast<size_t>(clamp_to(value, 0, count - 1))];
			const size_t copied = std::min(name.size(), sizeof(value_buf[i]));
			std::memcpy(value_buf[i], name.data(), copied);
			texts[i].value = std::string_view{value_buf[i], copied};
			cols[i].render = deluge::ui::oled::text_column;
			cols[i].ctx = &texts[i];
			break;
		}
		case Kind::Decimal:
			texts[i].value = format_decimal(value, kid.bind.decimal_places, value_buf[i], sizeof(value_buf[i]));
			cols[i].render = deluge::ui::oled::text_column;
			cols[i].ctx = &texts[i];
			break;
		default: {
			// A plain number is a `meter_column` with `Hint::None`, not a `text_column`
			// (`ui/oled/columns.h`'s note on the missing fifth arm).
			const IntRange range = bindings::range(kid, ctx_);
			meters[i].hint = kid.hint;
			meters[i].norm = normalised(value, range);
			meters[i].raw = value;
			meters[i].raw_max = range.max; // `Hint::Ducking` is the one hint that reads this
			cols[i].render = deluge::ui::oled::meter_column;
			cols[i].ctx = &meters[i];
			break;
		}
		}
	}

	TextScratch title_scratch;
	// `f.bounds()`, not `menu_rect(f)`: this widget owns the whole panel including its own title
	// band (`ui/oled/columns.h:139`).
	st_.render_resp =
	    deluge::ui::oled::columns(f, f.bounds(), st_.columns, std::span<const deluge::ui::oled::Column>{cols, n},
	                              text_of(node.title, title_scratch));
}

void MenuScene::render_leaf_int(deluge::ui::oled::OledFrame& f, const Node& node) {
	TextScratch scratch;
	deluge::ui::oled::title(f, text_of(node.title, scratch));

	const deluge::ui::Response r = deluge::ui::oled::int_value(f, deluge::ui::oled::menu_rect(f), st_.edit_int,
	                                                           bindings::range(node, ctx_), node.bind.style,
	                                                           /*focused=*/true);
	st_.render_resp = r;
	if (r.changed) {
		st_.value_changed = true;
		st_.value_node = &node;
	}
	// `int_value` owns the gold knobs and nothing else, so the select press that leaves the item
	// (`MenuItem::selectButtonPress` returning nullptr -> `goUpOneLevel`) is this scene's to claim.
	if (f.select_pressed()) {
		st_.render_resp.entered = true;
	}
}

void MenuScene::render_leaf_decimal(deluge::ui::oled::OledFrame& f, const Node& node) {
	TextScratch scratch;
	deluge::ui::oled::title(f, text_of(node.title, scratch));

	// `f.bounds()`, NOT `menu_rect(f)`: `decimal_value` draws at a canvas-absolute y and honours
	// the rect horizontally only (`ui/oled/values.h:115`), and its goldens were recorded that way.
	const deluge::ui::Response r =
	    deluge::ui::oled::decimal_value(f, f.bounds(), st_.edit_int, bindings::range(node, ctx_),
	                                    node.bind.decimal_places, st_.decimal_pos, st_.cursor_blink,
	                                    /*focused=*/true);
	st_.render_resp = r;
	if (r.changed) {
		st_.value_changed = true;
		st_.value_node = &node;
	}
	if (f.select_pressed()) {
		st_.render_resp.entered = true;
	}
}

void MenuScene::render_leaf_enum(deluge::ui::oled::OledFrame& f, const Node& node) {
	TextScratch scratch;
	deluge::ui::oled::title(f, text_of(node.title, scratch));

	std::string_view names_buf[kMaxDynamicNames];
	TextScratch names_scratch[kMaxDynamicNames];
	const size_t n = bindings::names(node, ctx_, names_buf, names_scratch);

	// `enum_value` takes a `ListState` AND a `ScrollText` of its own and has no `focused`
	// parameter -- it owns the turn and the select itself (`ui/oled/values.h:143`).
	const deluge::ui::Response r =
	    deluge::ui::oled::enum_value(f, deluge::ui::oled::menu_rect(f), st_.enum_index,
	                                 std::span<const std::string_view>{names_buf, n}, st_.enum_list, st_.enum_scroll,
	                                 /*wrap=*/false);
	st_.render_resp = r;
	if (r.changed) {
		st_.value_changed = true;
		st_.value_node = &node;
	}
}

void MenuScene::render_leaf_toggle(deluge::ui::oled::OledFrame& f, const Node& node) {
	TextScratch scratch;
	deluge::ui::oled::title(f, text_of(node.title, scratch));

	TextScratch off_scratch;
	TextScratch on_scratch;
	const std::string_view off = text_of(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_OFF), off_scratch);
	const std::string_view on = text_of(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ON), on_scratch);

	// A `bool&`, no `focused`; a select press reports `entered` and leaves `changed` false
	// (`ui/oled/values.h:169`).
	const deluge::ui::Response r =
	    deluge::ui::oled::toggle(f, deluge::ui::oled::menu_rect(f), st_.edit_bool, off, on, st_.scroll);
	st_.render_resp = r;
	if (r.changed) {
		st_.value_changed = true;
		st_.value_node = &node;
	}
}

void MenuScene::render_grid(deluge::ui::grid::GridFrame& f) {
	// Reads no input at all. That is what keeps "a scene reads any one input in at most one render
	// pass per tick" (`shell/README.md`) true for a scene drawn on two surfaces: the OLED pass owns
	// turn/select/back/edit, and this pass owns nothing.
	const Node* node = focused();
	if (node == nullptr) {
		return;
	}
	const uint32_t now = f.now_ms();

	// Two cadences, because legacy alternates them on one counter and one `Blink` carries one
	// period: the focused param's pad at a 180 ms re-arm (`sound_editor.cpp:985`), the patching
	// sources at 20 ms (`:996`).
	const ShortcutPos pos = shortcut_pos(ctx_.root, *node);
	if (pos.valid && blink_on(st_.shortcut_blink, now)) {
		(void)f.pad(pos.x, pos.y, kFlashColour);
	}
	if (blink_on(st_.source_blink, now)) {
		for (const ShortcutPos& source : source_shortcut_pads(ctx_.root, *node)) {
			(void)f.pad(source.x, source.y, kFlashColour);
		}
	}
}

void MenuScene::leds(deluge::ui::LedFrame& f) {
	// Button LEDs only: `LedFrame` has no pad concept whatever, so the pad blink above is grid
	// pixels rather than an LED declaration. The set is `SoundEditor::setLedStates`
	// (`sound_editor.cpp:301-314`), minus the parts that belong to services (`playbackHandler`,
	// `view.setKnobIndicatorLevels`).
	constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
	constexpr ButtonId kOffButtons[] = {ButtonId::Save, ButtonId::Synth,       ButtonId::Kit,  ButtonId::Midi,
	                                    ButtonId::Cv,   ButtonId::CrossScreen, ButtonId::Scale};
	for (ButtonId b : kOffButtons) {
		(void)f.set(static_cast<deluge::ui::LedId>(b), kOff);
	}
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back),
	            deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, kBackBlinkMs, 0});
}

// ---------------------------------------------------------------------------------------------
// Navigation and commits: the one place the render pass's observations touch the model.

void MenuScene::enter_leaf(const Node& node) {
	switch (node.kind) {
	case Kind::Enum:
		st_.enum_index = bindings::read(node, ctx_);
		st_.enum_list = deluge::ui::ListState{};
		st_.enum_scroll = deluge::ui::ScrollText{};
		break;
	case Kind::Toggle:
		st_.edit_bool = bindings::read(node, ctx_) != 0;
		st_.scroll = deluge::ui::ScrollText{};
		break;
	case Kind::Decimal:
		st_.edit_int = bindings::read(node, ctx_);
		st_.decimal_pos = 0;
		st_.cursor_blink = deluge::ui::Blink{};
		break;
	default:
		st_.edit_int = bindings::read(node, ctx_);
		break;
	}
}

void MenuScene::commit_value(const Node& node) {
	switch (node.kind) {
	case Kind::Enum:
		bindings::write(node, ctx_, st_.enum_index);
		break;
	case Kind::Toggle:
		bindings::write(node, ctx_, st_.edit_bool ? 1 : 0);
		break;
	default:
		bindings::write(node, ctx_, st_.edit_int);
		break;
	}
}

void MenuScene::enter_child(const Node& child) {
	switch (permission_for(child, ctx_)) {
	case Permission::No:
		return;
	case Permission::MustSelectRange:
		// Recorded, not acted on: E2 owns the range picker (legacy's `multiRangeMenu` redirect via
		// `menuItemHeadingTo`). Leaving the node unentered is the documented E1 behaviour -- the
		// alternative, entering it anyway, would edit whichever range happened to be first.
		st_.range_picker_pending = true;
		st_.pending_after_range = &child;
		return;
	case Permission::Yes:
		break;
	}

	if (child.kind == Kind::Action) {
		// An action runs where it stands; there is nothing to descend into.
		if (child.bind.set != nullptr) {
			child.bind.set(ctx_, 0);
		}
		return;
	}
	if (st_.depth + 1 >= kMenuDepth) {
		return;
	}
	st_.depth++;
	st_.path[st_.depth] = &child;
	st_.list[st_.depth] = deluge::ui::ListState{};
	st_.columns = deluge::ui::ColumnsState{};
	st_.scroll = deluge::ui::ScrollText{};
	if (is_leaf(child.kind)) {
		enter_leaf(child);
	}
}

void MenuScene::ascend() {
	// `SoundEditor::goUpOneLevel` (`sound_editor.cpp:672-683`): step up, skipping any level whose
	// permission has since become `No`. The cursor each level is restored to is the one
	// `st_.list[depth]` has been holding all along, which is what legacy's `focusChild`
	// (`submenu.cpp:21`) reconstructs from the item it navigated backward from.
	do {
		if (st_.depth == 0) {
			return;
		}
		st_.path[st_.depth] = nullptr;
		st_.depth--;
	} while (st_.path[st_.depth] != nullptr && permission_for(*st_.path[st_.depth], ctx_) == Permission::No);
}

Result MenuScene::after_render() {
	Result r;
	const Node* node = focused();
	if (node == nullptr) {
		return r;
	}

	if (st_.value_changed && st_.value_node != nullptr) {
		commit_value(*st_.value_node);
		// The write raises its own `notify::Dirty` facts (`bindings.cpp`), which repaint the
		// surfaces those name; `RepaintAll` covers what a commit changed *off* this surface (a
		// mod-knob indicator, the automation grid). Never `Action{Repaint, SurfaceRole::None}`,
		// which is a documented no-op (`shell/scene.h:165`).
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}

	// The horizontal menu's "notification value" popup on a column change
	// (`horizontal_menu.cpp:345`'s `switchVisiblePage` / `selectMenuItem` path). `notice_text`
	// copies the string into the queue's own 48-byte slot, so the stack buffer below is enough;
	// `notices().push` is the wrong call here -- it is `[[nodiscard]]` and drops silently
	// (`notify/notice_queue.h:46`).
	if (st_.render_resp.changed && node->kind == Kind::Submenu && node->layout == Layout::Horizontal) {
		if (const Node* column = selected_child(*node); column != nullptr) {
			char buf[16];
			std::string_view text;
			switch (column->kind) {
			case Kind::Decimal:
				text = format_decimal(bindings::read(*column, ctx_), column->bind.decimal_places, buf, sizeof(buf));
				break;
			case Kind::Submenu:
				break;
			default:
				text = format_int(bindings::read(*column, ctx_), buf, sizeof(buf));
				break;
			}
			if (!text.empty()) {
				deluge::notify::notice_text(text);
			}
		}
	}

	if (st_.render_resp.entered) {
		if (node->kind == Kind::Submenu) {
			if (const Node* child = selected_child(*node); child != nullptr) {
				enter_child(*child);
			}
		}
		else {
			// A leaf's select press goes up, it does not re-enter: `MenuItem::selectButtonPress`
			// returns nullptr and `SoundEditor` reads that as "up". `toggle`'s own doc says the
			// same thing from the widget's side -- a press reports `entered` and leaves `changed`
			// false, so this must not flip the value (`ui/oled/values.h:169`).
			ascend();
		}
	}
	else if (st_.back_claimed) {
		// Only ever set at depth > 0 (see `render_oled`); at depth 0 the back was never claimed
		// and `handle()` pops the scene instead.
		ascend();
	}

	st_.render_resp = {};
	st_.value_changed = false;
	st_.value_node = nullptr;
	st_.back_claimed = false;
	return r;
}

Result MenuScene::handle(const UiEvent& event, const UiContext& ctx) {
	// Legacy's `UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR` as a level rather than a mode
	// (`gui/menu_item/unpatched_param.cpp:52`). The AFFECT-ENTIRE button is an ordinary button, so
	// its own press and release both reach here and keep this in step before any write path runs;
	// `bindings::write` is the only reader.
	ctx_.affect_entire_held = ctx.held.has(ButtonId::AffectEntire);

	// (a) The shortcut-pad jump. A `Pad` event is never masked (`ui/core/frame_context.h`, rule
	// 4), so it always arrives here. Legacy gates the non-auditioning case on
	// `Buttons::isShiftButtonPressed()` (`sound_editor.cpp:1114`); the auditioning half is a UI
	// mode this layer may not read, so shift is the whole gate.
	if (event.kind == UiEvent::Kind::Pad && event.on && ctx.shift) {
		if (const Node* target = shortcut_node(ctx_.root, ctx.shift, event.x, event.y); target != nullptr) {
			retarget(*target);
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
			return r;
		}
		return Result::unhandled();
	}

	// (b) Back at the root. `render_oled` deliberately never claimed it at depth 0, so the shell
	// delivers it post-render and this closes the editor -- `sound_editor.cpp:418`'s BACK case ->
	// `:433` `goUpOneLevel()` -> `:678` `exitCompletely()`. `pop_to` stays `SceneKind::Null`,
	// i.e. "just the top".
	if (event.kind == UiEvent::Kind::Button && event.button == ButtonId::Back && event.on && st_.depth == 0) {
		Result r;
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Pop});
		return r;
	}

	return Result::unhandled();
}

// ---------------------------------------------------------------------------------------------
// Composition, saved state and timers

deluge::ui::grid::GreyoutMask MenuScene::greyout_mask() const {
	// `SoundEditor::getGreyoutColsAndRows` (`sound_editor.cpp:214-256`), with the arms this layer
	// can decide. The KEYBOARD_SCREEN, AUTOMATION and note-editor arms all branch on the root UI's
	// type or on a `currentUIMode`, neither of which `src/scenes` may read (the same wall that
	// kept `setup()`'s two `currentUIMode` roots out of `MenuContext::Root`); they stay with the
	// scene that owns those views. `doGreyout == false` is spelled as an all-zero mask, which
	// `greyout()` already reads as "grey nothing".
	if (ctx_.is_performance) {
		return {}; // `:248` PERFORMANCE
	}
	if (ctx_.clip != nullptr && !ctx_.is_song_view) {
		return {0xFFFFFFFEu, 0}; // `:236` INSTRUMENT_CLIP, the plain sound-editor arm
	}
	return {0xFFFFFFFFu, 0}; // `:252` default
}

void MenuScene::save_state(SceneState& state) const {
	MenuSavedState saved;
	if (st_.path[0] != nullptr) {
		saved.root_node_id = node_id(*st_.path[0]);
	}
	saved.depth = st_.depth;
	for (uint8_t level = 0; level < st_.depth && level + 1 < kMenuDepth; level++) {
		const Node* parent = st_.path[level];
		const Node* wanted = st_.path[level + 1];
		if (parent == nullptr || wanted == nullptr) {
			break;
		}
		const Node* kids[kMaxMenuChildren];
		const size_t n = visible_children(*parent, kids);
		for (size_t i = 0; i < n; i++) {
			if (kids[i] == wanted) {
				saved.path_child[level] = static_cast<uint8_t>(i);
				break;
			}
		}
	}
	const Node* node = focused();
	saved.focus = (node != nullptr && node->kind == Kind::Submenu && node->layout == Layout::Horizontal)
	                  ? st_.columns.focused
	                  : st_.list[st_.depth].cursor;
	std::memcpy(state.bytes, &saved, sizeof(saved));
}

void MenuScene::restore_state(const SceneState& state) {
	MenuSavedState saved;
	std::memcpy(&saved, state.bytes, sizeof(saved));

	const Node* root = node_by_id(saved.root_node_id);
	if (root == nullptr) {
		return;
	}
	retarget(*root);

	for (uint8_t level = 0; level < saved.depth && level < kMenuDepth - 1; level++) {
		const Node* child = child_at(*st_.path[level], saved.path_child[level]);
		if (child == nullptr) {
			break; // the table changed under the save; stop at the deepest level that still exists
		}
		st_.depth = static_cast<uint8_t>(level + 1);
		st_.path[st_.depth] = child;
		if (is_leaf(child->kind)) {
			enter_leaf(*child);
		}
	}

	const Node* node = focused();
	if (node != nullptr && node->kind == Kind::Submenu && node->layout == Layout::Horizontal) {
		st_.columns.focused = static_cast<int8_t>(saved.focus);
	}
	else {
		st_.list[st_.depth].cursor = saved.focus;
	}
}

std::optional<uint32_t> MenuScene::next_timer() const {
	// The blinks are the only thing here that moves without input, so the timer is the nearer of
	// the two *armed* cadences. A `Blink` with nothing to paint is not armed: waking the shell
	// every 20 ms to draw no source pads would be a battery cost with no pixels behind it.
	const Node* node = focused();
	if (node == nullptr) {
		return std::nullopt;
	}
	if (!source_shortcut_pads(ctx_.root, *node).empty()) {
		return kSourceBlinkMs;
	}
	if (shortcut_pos(ctx_.root, *node).valid) {
		return kShortcutBlinkMs;
	}
	return std::nullopt;
}

} // namespace deluge::scenes::menu
