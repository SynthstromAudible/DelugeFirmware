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

#include "scenes/rename/rename_scene.h"

#include "scenes/profile.h"

#include "ui/core/metrics.h"
#include "ui/oled/popup.h"
#include "ui/oled/text.h"

#include "gui/l10n/strings.h"

#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/output.h"
#include "model/song/song.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <array>

namespace deluge::scenes::rename {

using deluge::l10n::String;
using deluge::ui::kFlashTime;
using deluge::ui::kTextSpacingX;
using deluge::ui::kTextSpacingY;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using gfx::oled::Rect;

namespace {

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// The three targets. Each block is one legacy `RenameUI` subclass, virtual for virtual.

/// @brief `getCurrentKit()` (`model/song/song.cpp`'s helper), reached through the context rather
///        than the `currentSong` global -- `context_for` already resolved the clip's output and
///        recorded that it is a kit (`context.cpp:143-145`).
Kit* kit_of(const menu::MenuContext& ctx) {
	return (ctx.is_kit && ctx.output != nullptr) ? static_cast<Kit*>(ctx.output) : nullptr;
}

// --- Clip: `RenameClipUI` (`gui/ui/rename/rename_clip_ui.cpp:29-42`) ---

bool clip_can(const menu::MenuContext& ctx) {
	// `RenameClipUI` has no `canRename` override, so legacy's answer is the base's unconditional
	// true. The null tests are the survivable spelling of what legacy would have done instead:
	// dereference `clip` and `clip->output` outright.
	return ctx.clip != nullptr && ctx.clip->output != nullptr;
}

std::string_view clip_read(const menu::MenuContext& ctx) {
	return ctx.clip->name; // `clip->name.c_str()`
}

bool clip_write(const menu::MenuContext& ctx, std::string_view name) {
	// "Don't allow duplicate names on clips of a single output."
	Clip* other = ctx.clip->output->getClipFromName(name);
	if (other != nullptr && other != ctx.clip) {
		return false;
	}
	ctx.clip->name = name;
	return true;
}

// --- Output: `RenameOutputUI` (`gui/ui/rename/rename_output_ui.cpp:29-41`) ---

bool output_can(const menu::MenuContext& ctx) {
	return ctx.output != nullptr && ctx.song != nullptr;
}

std::string_view output_read(const menu::MenuContext& ctx) {
	return ctx.output->name; // `output->name.c_str()`
}

bool output_write(const menu::MenuContext& ctx, std::string_view name) {
	// "Duplicate names not allowed for audio outputs." Legacy asks the `currentSong` global; the
	// context's own `song` is the same object and is what this layer is allowed to read
	// (`check_layer_rules.py`'s `scenes-currentSong` row).
	void* other = ctx.song->getAudioOutputFromName(name);
	if (other != nullptr && other != ctx.output) {
		return false;
	}
	ctx.output->name = name;
	return true;
}

// --- Drum: `RenameDrumUI` (`gui/ui/rename/rename_drum_ui.cpp:31-58`) ---

bool drum_can(const menu::MenuContext& ctx) {
	// `RenameDrumUI` has no `canRename` override either; instead `getCurrentName` and `trySetName`
	// each `FREEZE_WITH_ERROR("RN01"/"RN02")` when there is no kit or no selected drum. A freeze is
	// not a behaviour worth porting, and `RenameUI::opened()` already has the arm that declines to
	// open -- so the same condition refuses the entry here.
	const Kit* kit = kit_of(ctx);
	return kit != nullptr && kit->selectedDrum != nullptr;
}

std::string_view drum_read(const menu::MenuContext& ctx) {
	return kit_of(ctx)->selectedDrum->drumName;
}

bool drum_write(const menu::MenuContext& ctx, std::string_view name) {
	Kit* kit = kit_of(ctx);
	// "We only allow renaming if there are no other drums with the same name."
	Drum* other = kit->getDrumFromName(name);
	if (other != nullptr && other != kit->selectedDrum) {
		return false;
	}
	kit->selectedDrum->drumName = name;
	return true;
}

constexpr std::array<Target, static_cast<size_t>(TargetId::Count)> kTargets{{
    // `RenameClipUI renameClipUI{"Clip Name"}` (`rename_clip_ui.cpp:27`).
    {sid(String::STRING_FOR_CLIP_NAME), clip_can, clip_read, clip_write, /*allow_empty=*/true},
    // `RenameOutputUI renameOutputUI{"Track name"}` (`rename_output_ui.cpp:29`).
    {sid(String::STRING_FOR_TRACK_NAME), output_can, output_read, output_write, /*allow_empty=*/true},
    // `RenameDrumUI renameDrumUI{"Drum Name"}` (`rename_drum_ui.cpp:32`), and the one
    // `allowEmpty() = false` in the family (`rename_drum_ui.h:33`).
    {sid(String::STRING_FOR_DRUM_NAME), drum_can, drum_read, drum_write, /*allow_empty=*/false},
}};

// ---------------------------------------------------------------------------------------------
// Small local helpers. `util/` is outside this layer's include allowance
// (`check_layer_rules.py`'s `scenes-includes-outside`), so the two things `qwerty_ui.cpp` borrows
// from it are spelled out here rather than included.

/// @brief `charCaseEqual` (`util/functions.cpp:1635`): equal ignoring ASCII letter case.
bool char_case_equal(char a, char b) {
	const char la = (a >= 'A' && a <= 'Z') ? static_cast<char>(a + 32) : a;
	const char lb = (b >= 'A' && b <= 'Z') ? static_cast<char>(b + 32) : b;
	return la == lb;
}

bool is_ascii_letter(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// @brief The whole canvas in @p f's local coordinates.
///
/// Every coordinate `RenameUI::renderOLED` computes is against `OLED_MAIN_WIDTH_PIXELS` /
/// `OLED_MAIN_HEIGHT_PIXELS` -- the *panel*, the rows the faceplate hides included -- so the
/// arithmetic below is done in this rectangle and lands in frame coordinates for free. Same
/// conversion `popup.cpp`'s own `panel_of` makes, for the same reason; it is file-static there.
Rect panel_of(const deluge::ui::oled::OledFrame& f) {
	const gfx::oled::Geometry g = f.canvas.geometry();
	return Rect{-f.origin.x, -f.origin.y, g.width, g.height};
}

/// @brief Is the blinking cursor lit at @p now? `values.cpp`'s `blink_lit`, which is
///        `OLED::performBlink`'s square wave.
bool blink_lit(const deluge::ui::Blink& b, uint32_t now) {
	return ((static_cast<uint32_t>(now - b.until_ms) / b.period_ms) & 1u) != 0;
}

uint32_t blink_next_flip_in(const deluge::ui::Blink& b, uint32_t now) {
	return b.period_ms - ((now - b.until_ms) % b.period_ms);
}

// `RenameUI::renderOLED` (`rename_ui.cpp:76-97`), as named constants.
constexpr int32_t kWindowW = 120;
constexpr int32_t kWindowH = 40;
/// `maxNumChars`, legacy's own literal: "'RENAME INSTRUMENT' should be the longest title string".
constexpr int32_t kMaxNumChars = 17;
/// Legacy's title row, `windowMinY + 6`.
constexpr int32_t kTitleDy = 6;
/// Legacy's name box, canvas rows 24..38 with `windowMinY == 4` on the shipped 48-row panel --
/// i.e. `windowMinY + 20` and `windowMinY + 34`. Expressed relative to the window rather than
/// canvas-absolute, because the two spellings agree exactly on the shipped geometry and only the
/// relative one survives a taller panel: at 64 rows `windowMinY` is 12 and legacy's absolute 24
/// would put the box *through* its own title.
constexpr int32_t kBoxDy = 20;
constexpr int32_t kBoxH = 14;
/// Legacy's `yPixel`, canvas row 27 -- three rows into the box, on either geometry.
constexpr int32_t kTextDy = kBoxDy + 3;

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

	// `RenameUI::opened` (`rename_ui.cpp:36-50`): refuse outright when the target says it cannot
	// be renamed, then seed the editor with the current name.
	if (!t.can(ctx_)) {
		return false;
	}

	// `QwertyUI::opened` (`qwerty_ui.cpp:54-55`) zeroes the edit position, and `RenameUI`'s
	// constructor zeroes the scroll (`rename_ui.cpp:28`); the cursor therefore starts at 0 with the
	// *whole* existing name to its right, which is what makes typing a character that matches the
	// one under the cursor simply step over it (see `type_char`).
	const std::string_view current = t.read(ctx_);
	len_ = static_cast<uint16_t>(std::min(current.size(), kMaxNameChars));
	std::copy_n(current.data(), len_, buf_);
	edit_ = deluge::ui::oled::TextEditState{};
	kb_ = deluge::ui::grid::TextKeyboardState{};
	resp_ = {};
	// `keyboardChars[util::to_underlying(FlashStorage::keyboardLayout)]` (`qwerty_ui.cpp:268`).
	// `KeyboardLayoutKind`'s values match legacy's enum exactly, so this is a cast (`keyboard.h`).
	layout_ = static_cast<deluge::ui::grid::KeyboardLayoutKind>(static_cast<uint8_t>(FlashStorage::keyboardLayout));
	// Unarm the blink: the first render arms it against that tick's clock, so the cursor is lit on
	// the frame the editor opens -- `setupBlink(..., shouldBlinkImmediately = true)`
	// (`qwerty_ui.cpp:151`, `hid/display/oled.cpp:844-846`).
	cursor_blink_ = deluge::ui::Blink{.period_ms = 0, .until_ms = 0};
	return true;
}

void RenameScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	(void)role;
	const Rect panel = panel_of(f);
	const uint32_t now = f.now_ms();
	last_render_ms_ = now;

	// `setupBlink(..., true)` inverts on the spot and then flips every `kFlashTime`; one
	// half-period of back-dating is how `values.cpp` spells "lit immediately".
	if (cursor_blink_.period_ms != kFlashTime) {
		cursor_blink_.period_ms = static_cast<uint16_t>(kFlashTime);
		cursor_blink_.until_ms = now - static_cast<uint32_t>(kFlashTime);
	}
	edit_.cursor_lit = blink_lit(cursor_blink_, now);

	// The window: 120x40, centred on the panel. `popup_frame` is `clearAreaExact` + `drawRectangle`
	// in one call, and `PopupStyle::Permanent` is its square-cornered arm -- legacy draws a square
	// rectangle here, not a rounded one (`popup.h`).
	const int32_t win_x = panel.x + ((panel.w - kWindowW) >> 1);
	const int32_t win_y = panel.y + ((panel.h - kWindowH) >> 1);
	deluge::ui::oled::popup_frame(f, Rect{win_x, win_y, kWindowW, kWindowH}, deluge::ui::oled::PopupStyle::Permanent);

	// `drawStringCentred(title, windowMinY + 6, kTextSpacingX, kTextSpacingY)` -- centred on the
	// *panel*, in the ordinary row font, NOT `drawScreenTitle`: there is no title rule here and the
	// text sits inside the box rather than across the top of the screen.
	deluge::ui::shell::TextScratch scratch;
	deluge::ui::oled::text(f, Rect{panel.x, win_y + kTitleDy, panel.w, kTextSpacingY},
	                       deluge_profile().text_or_digits(target(target_).title, scratch),
	                       {.font = f.style.row_font, .align = deluge::ui::oled::Align::Centre});

	// The name box and the text inside it. `charsStartPixel = (width - 17 * kTextSpacingX) >> 1`,
	// `boxStartPixel = charsStartPixel - 3`, and the box's right edge mirrors its left.
	const int32_t chars_off = (panel.w - kMaxNumChars * kTextSpacingX) >> 1;
	const int32_t chars_x = panel.x + chars_off;
	const int32_t box_off = chars_off - 3;
	deluge::ui::oled::popup_frame(f, Rect{panel.x + box_off, win_y + kBoxDy, panel.w - 2 * box_off, kBoxH},
	                              deluge::ui::oled::PopupStyle::Permanent);

	// `drawTextForOLEDEditing(charsStartPixel, WIDTH - charsStartPixel + 1, 27, 17, canvas)`: the
	// rect's right edge is legacy's `xPixelMax`, and its width in whole characters is legacy's
	// `maxNumChars`.
	const int32_t chars_w = panel.w - 2 * chars_off + 1;
	deluge::ui::oled::text_edit(f, Rect{chars_x, win_y + kTextDy, chars_w, kTextSpacingY}, entered_text(), edit_);
}

void RenameScene::render_grid(deluge::ui::grid::GridFrame& f) {
	// `QwertyUI::drawKeys` (`qwerty_ui.cpp:75-125`), already ported. The sidebar is deliberately
	// untouched -- legacy clears the *main* pads only.
	resp_ = deluge::ui::grid::text_keyboard(f, kb_, layout_, deluge::ui::grid::kTextKeyboardHomeRow);
}

Result RenameScene::after_render() {
	const deluge::ui::Response resp = resp_;
	resp_ = {};

	if (resp.index < 0) {
		return {};
	}
	const char c = static_cast<char>(resp.index);

	if (c == deluge::ui::grid::kTextKeyboardEnter) {
		// `QwertyUI::padAction`'s enter arm (`qwerty_ui.cpp:231-252`) -> `enterKeyPress()`.
		return enter_key_press();
	}

	if (c == deluge::ui::grid::kTextKeyboardBackspace) {
		// `qwerty_ui.cpp:204-229` -> `processBackspace()`. The 500 ms/80 ms auto-repeat legacy arms
		// alongside it (`UI_MODE_HOLDING_BACKSPACE` plus `TimerName::UI_SPECIFIC`,
		// `qwerty_ui.cpp:218-227,449-453`) is deliberately NOT ported: `text_keyboard`'s aggregate
		// `Response` reports "some keyboard pad is held", not *which*, so there is nothing here to
		// hang a per-pad repeat on. A widget-level hold (the `Hold`/`gesture` pair in
		// `ui/grid/effects.h`) is where it belongs, and it is the one legacy behaviour this scene
		// leaves on the table.
		backspace();
	}
	else {
		type_char(c);
	}

	// Nothing a widget drew is stale-proof: the edited string changed under it, so ask for the
	// next frame explicitly -- the same reason `MenuScene::after_render` ends with a `RepaintAll`.
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	return r;
}

Result RenameScene::handle(const UiEvent& event, const UiContext& ctx) {
	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// `RenameUI::buttonAction` (`rename_ui.cpp:104-134`). BACK exits without writing; the
		// select-encoder press commits. Both arms are `on`-only and both are gated on
		// `!currentUIMode`, which this scene has no equivalent of -- the modes legacy tests for
		// (`UI_MODE_HOLDING_BACKSPACE`, the prediction modes) are all QwertyUI's own and none of
		// them survive the port.
		if (!event.on) {
			break;
		}
		if (event.button == ButtonId::Back) {
			return close();
		}
		if (event.button == ButtonId::SelectEnc) {
			return enter_key_press();
		}
		break;

	case UiEvent::Kind::Pad:
		// `RenameUI::padAction` (`rename_ui.cpp:136-152`): a main pad goes to the keyboard --
		// which, in this stack, already claimed it while `render_grid` painted it, so anything
		// arriving here is by definition outside the keyboard's columns. "Otherwise, exit."
		if (event.on && event.x >= deluge::ui::grid::kTextKeyboardCols) {
			return close();
		}
		break;

	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::EncoderRole::Horizontal) {
			// `QwertyUI::horizontalEncoderAction` (`qwerty_ui.cpp:420-447`) with the two
			// `predictExtendedText()` calls dropped: `QwertyUI::predictExtendedText` is a
			// do-nothing `return true` for everything but `Browser` (`qwerty_ui.h:47`), so on a
			// rename the right-hand arm is exactly "already at the far right end, get out".
			const int32_t at = edit_.cursor;
			if ((event.delta > 0 && at >= len_) || (event.delta < 0 && at == 0) || event.delta == 0) {
				Result r;
				r.handled = true;
				return r;
			}
			// Legacy adds the offset unclamped, which an accelerated turn can push past the end of
			// the string -- `enteredText.c_str()[enteredTextEditPos]` would then read past the
			// terminator. Clamped here; every in-range turn lands where legacy's does.
			edit_.cursor = static_cast<int16_t>(std::clamp<int32_t>(at + event.delta, 0, len_));
			Result r;
			r.handled = true;
			(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			return r;
		}
		if (event.encoder == deluge::ui::EncoderRole::Vertical) {
			// `RenameUI::verticalEncoderAction` (`rename_ui.cpp:154-156`): swallowed, so a turn
			// does not scroll whatever view is showing underneath.
			Result r;
			r.handled = true;
			return r;
		}
		break;

	case UiEvent::Kind::AllReleased:
		break;
	}
	(void)ctx;
	return Result::unhandled();
}

void RenameScene::type_char(char c) {
	const int32_t pos = edit_.cursor;

	// "First character must be alphanumerical" (`qwerty_ui.cpp:273-280`).
	if (pos == 0 && !((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
		return;
	}

	// "If this char was already predicted, that's easy and we can just move forward"
	// (`qwerty_ui.cpp:305-317`). On a rename there is no prediction, but the editor opens with the
	// existing name to the right of the cursor, so this is what lets a user retype the prefix of a
	// name they are only partly changing and keep the tail.
	if (pos < len_ && char_case_equal(buf_[pos], c)) {
		edit_.cursor = static_cast<int16_t>(pos + 1);
		return;
	}

	char nc = c;
	// "If char is a letter... If following another letter, make it lowercase"
	// (`qwerty_ui.cpp:334-343`).
	if (nc >= 'A' && nc <= 'Z' && pos > 0 && is_ascii_letter(buf_[pos - 1])) {
		nc = static_cast<char>(nc + 32);
	}

	if (pos >= static_cast<int32_t>(kMaxNameChars)) {
		return; // the buffer's own ceiling; see `kMaxNameChars`
	}
	// `enteredText.resize(enteredTextEditPos); enteredText.append(...)` (`qwerty_ui.cpp:350-353`):
	// typing mid-string truncates the tail rather than inserting.
	len_ = static_cast<uint16_t>(pos);
	buf_[len_++] = nc;
	edit_.cursor = static_cast<int16_t>(len_);
}

void RenameScene::backspace() {
	// `QwertyUI::processBackspace` (`qwerty_ui.cpp:408-418`), including its asymmetry: the edit
	// position steps back whether or not there is any text, and the string is only truncated when
	// there is.
	if (edit_.cursor > 0) {
		edit_.cursor--;
	}
	if (len_ != 0) {
		len_ = static_cast<uint16_t>(edit_.cursor);
	}
}

Result RenameScene::enter_key_press() {
	// `RenameUI::enterKeyPress` (`rename_ui.cpp:55-62`).
	const Target& t = target(target_);
	Result r;
	r.handled = true;

	// "if (enteredText.empty() && !allowEmpty()) return;" -- silently, with no popup. Only the
	// drum target refuses an empty name, and legacy tells the user nothing about it, so neither
	// does this.
	if (len_ == 0 && !t.allow_empty) {
		return r;
	}

	if (t.write(ctx_, entered_text())) {
		return close();
	}

	// The rejection. Legacy raises `STRING_FOR_DUPLICATE_NAMES` from inside `trySetName` itself
	// (`display->displayPopup(...)` in all three subclasses) and then returns false, which leaves
	// `enterKeyPress` doing nothing at all -- the editor stays open with the name still typed. The
	// popup is raised here rather than in `write` because `Action::Kind::Popup` is how a scene
	// spells a popup (`shell/scene.h`) and a plain `bool (*)(...)` cannot return one; all three
	// targets raise the same string, so nothing is lost by hoisting it.
	//
	// (The brief called for a `notify::Severity::Warn` notice instead. There is no such path: the
	// raise-side helpers in `notify/notice_queue.h` take no severity at all, and the shell's own
	// popup action is the direct port of `displayPopup`.)
	(void)r.actions.push_back(Action{.kind = Action::Kind::Popup,
	                                 .surface = SurfaceRole::Main,
	                                 .popup_string = sid(String::STRING_FOR_DUPLICATE_NAMES)});
	return r;
}

Result RenameScene::close() {
	// `RenameUI::exitUI` -> `close()`. `Remove` naming this scene's own kind, not `Pop`: a notice
	// overlay can legitimately be pushed above this scene between the tick that decided to close
	// and the router applying it, and `Pop` would take that down too (`shell/scene.h`'s note on
	// `Action::Kind::Pop`, and `MenuScene::handle`'s root-`Back` case).
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kRenameSceneKind});
	return r;
}

std::optional<uint32_t> RenameScene::next_timer() const {
	// The cursor bar is the only thing here that moves without input, and it is only drawn when the
	// cursor sits at the end of the string (`text_edit`: `cursor_lit` gates that arm and nothing
	// else). Mid-string there is nothing to flash, so nothing to wake for.
	if (cursor_blink_.period_ms == 0 || edit_.cursor != static_cast<int16_t>(len_)) {
		return std::nullopt;
	}
	return blink_next_flip_in(cursor_blink_, last_render_ms_);
}

RenameScene& rename_scene() {
	static RenameScene scene;
	return scene;
}

} // namespace deluge::scenes::rename
