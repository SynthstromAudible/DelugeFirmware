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
/// @brief The `Target` table and the scene. See `rename_scene.h`.

#include "ui/scenes/rename/rename_scene.h"

#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/bind/target.h"
#include "ui/scenes/profile.h"
#include "ui/views/grid/keyboard.h"
#include "ui/views/oled/rename.h"

#include "ui/widgets/core/blink.h"
#include "ui/widgets/core/metrics.h"

#include "gui/l10n/strings.h"

#include "project/device_settings.h"
#include "project/setting_group.h"
#include "ui/scenes/bind/device_reader.h"

#include <algorithm>
#include <array>
#include <utility>

namespace deluge::ui::scenes::rename {

using deluge::l10n::String;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using deluge::ui::widgets::core::kFlashTime;

namespace {

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// The three targets. Each block is one legacy `RenameUI` subclass, virtual for virtual: the name is its owner's
// record's `name` row, read from the record and written as a text request; the sink refuses a name the owner's
// siblings already have, as each subclass's `trySetName` does.

constexpr bind::Target name_row(project::SettingGroupId group) {
	return {.kind = bind::Target::Kind::MODEL, .path = "name", .group = group};
}

/// The name @p group's record holds for the owner @p ctx names; nullopt where it names none.
std::optional<std::string> name_in(const menu::MenuContext& ctx, project::SettingGroupId group) {
	if (ctx.model == nullptr) {
		return std::nullopt;
	}
	const std::optional<project::SettingAddress> address = bind::setting_address(name_row(group), ctx.addresses);
	if (!address) {
		return std::nullopt;
	}
	const std::optional<serialization::Value> value = project::read_setting(*ctx.model, *address, "name");
	return value ? std::optional{value->str()} : std::nullopt;
}

bool write_name(const menu::MenuContext& ctx, project::SettingGroupId group, std::string_view name) {
	return ctx.ports.writes != nullptr && ctx.ports.writes->write_text(name_row(group), ctx.addresses, name);
}

// --- Clip: `RenameClipUI` (`gui/ui/rename/rename_clip_ui.cpp`) ---

bool clip_can(const menu::MenuContext& ctx) {
	// `RenameClipUI` has no `canRename` override, so legacy's answer is the base's unconditional true; a context with
	// no clip has no name to edit.
	return name_in(ctx, project::SettingGroupId::CLIP).has_value();
}

std::string clip_read(const menu::MenuContext& ctx) {
	return name_in(ctx, project::SettingGroupId::CLIP).value_or(std::string{});
}

bool clip_write(const menu::MenuContext& ctx, std::string_view name) {
	return write_name(ctx, project::SettingGroupId::CLIP, name); // no two clips of a track share a name
}

// --- Output: `RenameOutputUI` (`gui/ui/rename/rename_output_ui.cpp`) ---

bool output_can(const menu::MenuContext& ctx) {
	return name_in(ctx, project::SettingGroupId::TRACK).has_value();
}

std::string output_read(const menu::MenuContext& ctx) {
	return name_in(ctx, project::SettingGroupId::TRACK).value_or(std::string{});
}

bool output_write(const menu::MenuContext& ctx, std::string_view name) {
	return write_name(ctx, project::SettingGroupId::TRACK, name); // no two audio tracks share a name
}

// --- Drum: `RenameDrumUI` (`origin/main:src/deluge/gui/ui/rename/rename_drum_ui.cpp`) ---

bool drum_can(const menu::MenuContext& ctx) {
	// `RenameDrumUI` has no `canRename` override either; instead `getCurrentName` and `trySetName` each
	// `FREEZE_WITH_ERROR("RN01"/"RN02")` when there is no kit or no selected drum. The same condition refuses the entry
	// here, which `RenameUI::opened()` already has an arm for.
	return ctx.is_kit && name_in(ctx, project::SettingGroupId::LANE).has_value();
}

std::string drum_read(const menu::MenuContext& ctx) {
	return name_in(ctx, project::SettingGroupId::LANE).value_or(std::string{});
}

bool drum_write(const menu::MenuContext& ctx, std::string_view name) {
	return write_name(ctx, project::SettingGroupId::LANE, name); // no two drums of a kit share a name
}

constexpr std::array<Target, static_cast<size_t>(TargetId::Count)> kTargets{{
    // `RenameClipUI renameClipUI{"Clip Name"}` (`rename_clip_ui.cpp`).
    {sid(String::STRING_FOR_CLIP_NAME), clip_can, clip_read, clip_write, /*allow_empty=*/true},
    // `RenameOutputUI renameOutputUI{"Track name"}` (`rename_output_ui.cpp`).
    {sid(String::STRING_FOR_TRACK_NAME), output_can, output_read, output_write, /*allow_empty=*/true},
    // `RenameDrumUI renameDrumUI{"Drum Name"}` (`origin/main:src/deluge/gui/ui/rename/rename_drum_ui.cpp`), and the one
    // `allowEmpty() = false` in the family (`RenameDrumUI::allowEmpty`).
    {sid(String::STRING_FOR_DRUM_NAME), drum_can, drum_read, drum_write, /*allow_empty=*/false},
}};

// ---------------------------------------------------------------------------------------------
// Small local helpers. `util/` is outside this layer's include allowance
// (`scripts/layers.toml`'s `scenes-includes-outside`), so the two things `qwerty_ui.cpp` borrows
// from it are spelled out here rather than included.

/// @brief `charCaseEqual` (`util/functions.cpp`): equal ignoring ASCII letter case.
bool char_case_equal(char a, char b) {
	const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a + 32) : a;
	const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b + 32) : b;
	return la == lb;
}

bool is_ascii_letter(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

} // namespace

const Target& target(TargetId id) {
	const size_t i = static_cast<size_t>(id);
	return kTargets[i < kTargets.size() ? i : 0];
}

// ---------------------------------------------------------------------------------------------

void RenameScene::open(const menu::MenuContext& ctx) {
	ctx_ = ctx;
}

bool RenameScene::on_enter(const SceneId& id) {
	if (id.arg >= static_cast<uint16_t>(TargetId::Count)) {
		return false;
	}
	target_ = static_cast<TargetId>(id.arg);
	const Target& t = target(target_);

	// `RenameUI::opened` (`rename_ui.cpp`): refuse outright when the target says it cannot
	// be renamed, then seed the editor with the current name.
	if (!t.can(ctx_)) {
		return false;
	}

	// `QwertyUI::opened` (`qwerty_ui.cpp`) zeroes the edit position, and `RenameUI`'s
	// constructor zeroes the scroll (`rename_ui.cpp`); the cursor therefore starts at 0 with the
	// *whole* existing name to its right, which is what makes typing a character that matches the
	// one under the cursor simply step over it (see `type_char`).
	const std::string current = t.read(ctx_);
	text_ = current;
	edit_ = deluge::ui::widgets::oled::TextEditState{};
	kb_ = deluge::ui::widgets::grid::TextKeyboardState{};
	resp_ = {};
	// `keyboardChars[util::to_underlying(FlashStorage::keyboardLayout)]` (`QwertyUI::padAction`): the text keyboard's
	// DEVICE row, whose choices are `KeyboardLayoutKind`'s values (`widgets/grid/keyboard.h`).
	layout_ = bind::read_device<deluge::ui::widgets::grid::KeyboardLayoutKind>(
	    ctx_.ports.device, project::device_path::kTextKeyboardLayout);
	// Unarm the blink: the first render arms it against that tick's clock, so the cursor is lit on
	// the frame the editor opens -- `setupBlink(..., shouldBlinkImmediately = true)`
	// (`QwertyUI::drawTextForOLEDEditing` in `qwerty_ui.cpp`, and `hid/display/oled.cpp`).
	cursor_blink_ = deluge::ui::widgets::core::Blink{};
	repeat_armed_ = false;
	repeat_at_ms_ = 0;
	blink_restart_ = true;
	return true;
}

void RenameScene::leds(deluge::ui::widgets::core::LedFrame& frame) {
	if (std::exchange(blink_restart_, false)) {
		frame.restart(deluge::ui::widgets::core::BlinkRate::Normal);
	}
	(void)frame.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Back),
	                deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Normal));
}

void RenameScene::on_exit() {
	// `open()` is the only writer of `ctx_`, and this scene is a singleton
	// that outlives every visit -- so without this a re-entry that forgot its `open()` would
	// validate `can()` against the *previous* song's `Clip`/`Kit`/`Output` pointers and quite
	// possibly open on freed memory. Legacy had the same shape but a much shorter fuse
	// (`renameClipUI.clip` was reassigned by `EditName::beginSession` immediately before every
	// `openUI`), so it never had to answer the question.
	//
	// Cleared rather than "make `on_enter` demand a fresh `open()`" via a separate flag, because
	// the two are the same test done twice: a zeroed `MenuContext` has no model and no addresses,
	// every target's `can` already rejects that, and `on_enter` already refuses when `can` says no.
	// One mechanism, and it fails closed -- an entry with no `open()` rolls the push back instead
	// of drawing an editor over a stale model.
	ctx_ = menu::MenuContext{};
	repeat_armed_ = false;
	grid_pass_ = false;
}

void RenameScene::render_oled(SurfaceRole role, deluge::ui::widgets::oled::OledFrame& f) {
	(void)role;
	const uint32_t now = f.now_ms();
	last_render_ms_ = now;

	// `setupBlink(..., true)` inverts on the spot and then flips every `kFlashTime`; one
	// half-period of back-dating is how `values.cpp` spells "lit immediately".
	if (!cursor_blink_.armed()) {
		cursor_blink_.period_ms = static_cast<uint16_t>(kFlashTime);
		cursor_blink_.restart_lit(now);
	}
	edit_.cursor_lit = deluge::ui::widgets::core::blink_lit(cursor_blink_, now);
	// Arm the panel's own gate at the cursor's *next flip*, which is what `next_timer()` cannot
	// name: it answers one interval for the scene, this names the edge. `text_edit` cannot arm it
	// itself either, because it takes no clock and owns no state (`ui/widgets/oled/text_edit.h`:
	// "Render-only"). Without `Shell::tick`'s step 8b the host would wake on `next_timer()` while
	// `RepaintGate` still said no, and the bar would be computed correctly and never drawn. Only while
	// the bar exists at all: mid-string `cursor_lit` gates nothing.
	if (edit_.cursor == static_cast<int16_t>(text_.size())) {
		f.gate().request_repaint_after(deluge::ui::widgets::core::blink_next_flip_in(cursor_blink_, now), now);
	}

	deluge::ui::shell::TextScratch scratch;
	const deluge::ui::views::oled::RenameWindow window = present_window(scratch);
	deluge::ui::views::oled::rename_window(f, window, edit_);
}

void RenameScene::render_grid(deluge::ui::widgets::grid::GridFrame& f) {
	// `QwertyUI::drawKeys` (`qwerty_ui.cpp`), already ported. The sidebar is deliberately
	// untouched -- legacy clears the *main* pads only.
	resp_ = deluge::ui::views::grid::keyboard(f, present_keys(), kb_);
	grid_pass_ = true;
	// Stamped here as well as in `render_oled`, and for a different reason: the backspace repeat
	// runs off the grid pass, which is the pass that reports the hold. A tick that repainted only
	// the pads would otherwise advance the repeat against `render_oled`'s stale clock.
	const uint32_t now = f.now_ms();
	last_render_ms_ = now;
	// And the grid's gate is what brings this pass back around while the pad is simply held down
	// with no input arriving -- the port of legacy re-arming `TimerName::UI_SPECIFIC` after every
	// repeat. `after_render` reads `resp_`, so the repeat can only advance on a tick this pass ran.
	if (repeat_armed_) {
		const int32_t due = static_cast<int32_t>(repeat_at_ms_ - now);
		f.gate().request_repaint_after(static_cast<uint32_t>(due > 0 ? due : 0), now);
	}
}

Result RenameScene::after_render() {
	const deluge::ui::widgets::core::Response resp = resp_;
	resp_ = {};

	// `QwertyUI::timerCallback`'s `UI_MODE_HOLDING_BACKSPACE` arm, ahead of this tick's own key:
	// legacy's timer and its pad handler are separate entry points and the timer fires on ticks
	// with no pad event at all, which is exactly the tick this arm exists for.
	// **Only on a tick the grid pass actually ran.** `resp_` is `render_grid`'s observation, and the
	// shell calls `after_render()` for every scene that drew on any surface -- so on an OLED-only
	// tick (the cursor blink's own repaint, or the menu underneath this overlay asking for one)
	// the pads were not read at all; see `RenameScene::grid_pass_`.
	const bool grid_pass = grid_pass_;
	grid_pass_ = false;
	bool changed = grid_pass && advance_repeat(kb_.backspace_held, last_render_ms_);

	// Keys and Enter are refused while the hold is up, backspace included: `QwertyUI::padAction` acts
	// on them only with no UI mode set.
	if (resp.index >= 0 && !repeat_armed_) {
		const char c = static_cast<char>(resp.index);

		if (c == deluge::ui::widgets::grid::kTextKeyboardEnter) {
			// `QwertyUI::padAction`'s enter arm (`qwerty_ui.cpp`) -> `enterKeyPress()`.
			return enter_key_press();
		}

		if (c == deluge::ui::widgets::grid::kTextKeyboardBackspace) {
			// `QwertyUI::padAction` (`qwerty_ui.cpp`): delete one character now, then arm the hold at
			// `deluge::ui::widgets::grid::kBackspaceRepeatDelayMs` -- legacy's `processBackspace()` followed by
			// `setTimer(UI_SPECIFIC, 500)`, with `repeat_armed_` standing in for
			// `currentUIMode = UI_MODE_HOLDING_BACKSPACE`.
			backspace();
			repeat_armed_ = true;
			repeat_at_ms_ = last_render_ms_ + deluge::ui::widgets::grid::kBackspaceRepeatDelayMs;
		}
		else {
			type_char(c);
		}
		changed = true;
	}

	if (!changed) {
		return {};
	}

	// Nothing a widget drew is stale-proof: the edited string changed under it, so ask for the
	// next frame explicitly -- the same reason `MenuScene::after_render` ends with a `RepaintAll`.
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	return r;
}

bool RenameScene::advance_repeat(bool backspace_held, uint32_t now) {
	if (!repeat_armed_) {
		return false;
	}
	// `QwertyUI::padAction`'s release arm leaves `UI_MODE_HOLDING_BACKSPACE` and unsets the timer. Here the
	// hold is a level: the keyboard reports whether a backspace pad is still down, so a letter pressed
	// beside it does not end the repeat, as in `main`, where the press is swallowed.
	if (!backspace_held) {
		repeat_armed_ = false;
		return false;
	}
	// Wrap-safe: the difference is taken as signed, so a clock that has wrapped past `repeat_at_ms_`
	// still reads as "due" rather than as "due in 49 days".
	if (static_cast<int32_t>(now - repeat_at_ms_) < 0) {
		return false;
	}
	// `QwertyUI::timerCallback`'s `UI_MODE_HOLDING_BACKSPACE` arm: one more deletion, then re-arm at the faster
	// cadence. Re-armed from `now` rather than from `repeat_at_ms_`, so a tick that arrived late
	// (a long render, a card access) deletes one character and not a burst catching up.
	backspace();
	repeat_at_ms_ = now + deluge::ui::widgets::grid::kBackspaceRepeatMs;
	return true;
}

Result RenameScene::handle(const UiEvent& event, const UiContext& ctx) {
	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// `RenameUI::buttonAction` (`rename_ui.cpp`). BACK exits without writing; the
		// select-encoder press commits. Both arms are `on`-only and both are gated on
		// `!currentUIMode`, which this scene has no equivalent of -- the modes legacy tests for
		// (`UI_MODE_HOLDING_BACKSPACE`, the prediction modes) are all QwertyUI's own and none of
		// them survive the port.
		if (!event.on) {
			break;
		}
		if (repeat_armed_) {
			// `RenameUI::buttonAction`: BACK and SELECT act only with no UI mode set.
			if (event.button == ButtonId::Back || event.button == ButtonId::SelectEnc) {
				Result r;
				r.handled = true;
				return r;
			}
			break;
		}
		if (event.button == ButtonId::Back) {
			return close();
		}
		if (event.button == ButtonId::SelectEnc) {
			return enter_key_press();
		}
		// The release edges of BACK and SELECT_ENC fall through to
		// `Result::unhandled()` where legacy answered `DEALT_WITH` for the whole button. This is bounded, and
		// deliberately so: `Router::run_chain` delivers only to `SceneStack::input_top()`, and this
		// scene is it (`input_transparent()` is false), so an unhandled release reaches nothing else.
		break;

	case UiEvent::Kind::Pad:
		// `RenameUI::padAction`: a main pad goes to the keyboard --
		// which, in this stack, already claimed it while `render_grid` painted it, so anything
		// arriving here is by definition outside the keyboard's columns. "Otherwise, exit."
		if (event.on && event.x >= deluge::ui::widgets::grid::kTextKeyboardCols) {
			if (repeat_armed_) {
				Result r; // `RenameUI::padAction` exits only with no UI mode set
				r.handled = true;
				return r;
			}
			return close();
		}
		break;

	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::widgets::core::EncoderRole::Horizontal) {
			// `QwertyUI::horizontalEncoderAction` with the two
			// `predictExtendedText()` calls dropped: `QwertyUI::predictExtendedText` is a
			// do-nothing `return true` for everything but `Browser` (`QwertyUI::predictExtendedText`), so on a
			// rename the right-hand arm is exactly "already at the far right end, get out".
			const int32_t at = edit_.cursor;
			if ((event.delta > 0 && at >= static_cast<int32_t>(text_.size())) || (event.delta < 0 && at == 0)
			    || event.delta == 0) {
				Result r;
				r.handled = true;
				return r;
			}
			// Legacy adds the offset unclamped, which an accelerated turn can push past the end of
			// the string -- `enteredText.c_str()[enteredTextEditPos]` would then read past the
			// terminator. Clamped here; every in-range turn lands where legacy's does.
			edit_.cursor =
			    static_cast<int16_t>(std::clamp<int32_t>(at + event.delta, 0, static_cast<int32_t>(text_.size())));
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			return r;
		}
		if (event.encoder == deluge::ui::widgets::core::EncoderRole::Vertical) {
			// `RenameUI::verticalEncoderAction`: swallowed, so a turn
			// does not scroll whatever view is showing underneath.
			Result r;
			r.handled = true;
			return r;
		}
		break;

	// The MIDI kinds (the transport in `ui/shell/scene.h`) are declined here for the reason
	// every scene ported so far declines them: claiming one means answering `learnable_at(pad)`,
	// which is the timeline scenes' work. Declining lets `Router::run_chain` carry the
	// message on to `services::MidiLearn`, which is where legacy's `view.` fallback puts it.
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
	case UiEvent::Kind::MidiProgramChange:
	case UiEvent::Kind::MidiPitchBend:
	case UiEvent::Kind::AllReleased:
		break;
	}
	(void)ctx;
	return Result::unhandled();
}

void RenameScene::type_char(char c) {
	const int32_t pos = edit_.cursor;

	// "First character must be alphanumerical" (`QwertyUI::padAction` in `qwerty_ui.cpp`).
	if (pos == 0 && !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
		return;
	}

	// "If this char was already predicted, that's easy and we can just move forward"
	// (`QwertyUI::padAction` in `qwerty_ui.cpp`). On a rename there is no prediction, but the editor opens with the
	// existing name to the right of the cursor, so this is what lets a user retype the prefix of a
	// name they are only partly changing and keep the tail.
	if (pos < static_cast<int32_t>(text_.size()) && char_case_equal(text_[pos], c)) {
		edit_.cursor = static_cast<int16_t>(pos + 1);
		return;
	}

	char nc = c;
	// "If char is a letter... If following another letter, make it lowercase"
	// (`QwertyUI::padAction` in `qwerty_ui.cpp`).
	if (nc >= 'A' && nc <= 'Z' && pos > 0 && is_ascii_letter(text_[pos - 1])) {
		nc = static_cast<char>(nc + 32);
	}

	// `enteredText.resize(enteredTextEditPos); enteredText.append(...)` (`QwertyUI::padAction` in `qwerty_ui.cpp`):
	// typing mid-string truncates the tail rather than inserting.
	text_.resize(static_cast<size_t>(pos));
	text_ += nc;
	edit_.cursor = static_cast<int16_t>(text_.size());
}

void RenameScene::backspace() {
	// `QwertyUI::processBackspace`, including its asymmetry: the edit
	// position steps back whether or not there is any text, and the string is only truncated when
	// there is.
	if (edit_.cursor > 0) {
		edit_.cursor--;
	}
	if (!text_.empty()) {
		text_.resize(static_cast<size_t>(edit_.cursor));
	}
}

Result RenameScene::enter_key_press() {
	// `RenameUI::enterKeyPress` (`rename_ui.cpp`).
	const Target& t = target(target_);
	Result r;
	r.handled = true;

	// "if (enteredText.empty() && !allowEmpty()) return;" -- silently, with no popup. Only the
	// drum target refuses an empty name, and legacy tells the user nothing about it, so neither
	// does this.
	if (text_.empty() && !t.allow_empty) {
		return r;
	}

	if (t.write(ctx_, entered_text())) {
		return close();
	}

	// The rejection. Legacy raises `STRING_FOR_DUPLICATE_NAMES` from inside `trySetName` itself
	// (`display->displayPopup(...)` in all three subclasses) and then returns false, which leaves
	// `enterKeyPress` doing nothing at all -- the editor stays open with the name still typed. The
	// popup is raised here rather than in `write` because `Action::Kind::Popup` is how a scene
	// spells a popup (`ui/shell/scene.h`) and a plain `bool (*)(...)` cannot return one; all three
	// targets raise the same string, so nothing is lost by hoisting it.
	//
	// (A `notify::Severity::Warn` notice is not an option. There is no such path: the
	// raise-side helpers in `base/notify/notice_queue.h` take no severity at all, and the shell's own
	// popup action is the direct port of `displayPopup`.)
	(void)r.actions.push_back(Action{.kind = Action::Kind::Popup,
	                                 .surface = SurfaceRole::Main,
	                                 .popup_string = sid(String::STRING_FOR_DUPLICATE_NAMES)});
	return r;
}

Result RenameScene::close() {
	// `RenameUI::exitUI` -> `close()`. `Remove` naming this scene's own kind, not `Pop`: a notice
	// overlay can legitimately be pushed above this scene between the tick that decided to close
	// and the router applying it, and `Pop` would take that down too (`ui/shell/scene.h`'s note on
	// `Action::Kind::Pop`, and `MenuScene::handle`'s root-`Back` case).
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kRenameSceneKind});
	return r;
}

std::optional<uint32_t> RenameScene::next_timer() const {
	std::optional<uint32_t> next;

	// The cursor bar moves without input, but only while the cursor sits at the end of the string
	// (`text_edit`: `cursor_lit` gates that arm and nothing else). Mid-string there is nothing to
	// flash, so nothing to wake for.
	if (cursor_blink_.armed() && edit_.cursor == static_cast<int16_t>(text_.size())) {
		next = deluge::ui::widgets::core::blink_next_flip_in(cursor_blink_, last_render_ms_);
	}

	// The held-backspace repeat is legacy's `TimerName::UI_SPECIFIC`, and it is the reason this
	// scene must be woken on a tick with no input at all: nothing else would run `render_grid`, and
	// `render_grid` is what reports the pad is still down.
	if (repeat_armed_) {
		const int32_t due = static_cast<int32_t>(repeat_at_ms_ - last_render_ms_);
		const auto in_ms = static_cast<uint32_t>(due > 0 ? due : 0);
		next = next.has_value() ? std::min(*next, in_ms) : in_ms;
	}
	return next;
}

RenameScene& rename_scene() {
	static RenameScene scene;
	return scene;
}

} // namespace deluge::ui::scenes::rename
