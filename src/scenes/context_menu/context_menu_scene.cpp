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
/// @brief The `Descriptor` table and the scene. See `context_menu_scene.h`.

#include "scenes/context_menu/context_menu_scene.h"

#include "scenes/profile.h"

#include "ui/core/led_frame.h"
#include "ui/core/metrics.h"
#include "ui/oled/popup.h"

// `Deluge::factoryReset` (`deluge.cpp:219`). `deluge.h` is a ROOT-LEVEL app header, so it is named
// explicitly in this layer's include allow-list (`src/scenes/CMakeLists.txt`) the way
// `definitions_cxx.hpp` is, rather than being covered by one of the allowed subdirectories. Fix
// round 1, Minor 2: included rather than re-declared locally, and re-implementing its body here (a
// notice plus the other four resets) was rejected -- that would fork a legacy composite.
#include "deluge.h"
#include "gui/l10n/strings.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_follow.h"
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace deluge::scenes::context_menu {

using deluge::l10n::String;
using deluge::ui::kFlashTime;
using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::TextScratch;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

namespace {

constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

// ---------------------------------------------------------------------------------------------
// The five descriptors. Each is one `ResetSettingsAction` arm of
// `ResetSettings::acceptCurrentOption` (`gui/context_menu/reset_settings.cpp:44-63`), with the
// heading and the option list that class's `getTitle()` / `getOptions()` return for all five.

/// `ResetSettings::getOptions()`: a single `STRING_FOR_OK`. **Not a NO/YES pair** -- legacy's
/// "are you sure" confirmation is one option you either press select on or back out of.
constexpr uint16_t kConfirmOptions[] = {sid(String::STRING_FOR_OK)};

bool accept_reset_flash(const menu::MenuContext&, uint8_t) {
	FlashStorage::factoryReset();
	return false; // `acceptCurrentOption` returns false -> the menu closes.
}

bool accept_reset_community_features(const menu::MenuContext&, uint8_t) {
	runtimeFeatureSettings.factoryReset();
	return false;
}

bool accept_reset_midi_follow(const menu::MenuContext&, uint8_t) {
	midiFollow.factoryReset();
	return false;
}

bool accept_reset_midi_devices(const menu::MenuContext&, uint8_t) {
	MIDIDeviceManager::factoryReset();
	return false;
}

bool accept_reset_all(const menu::MenuContext&, uint8_t) {
	Deluge::factoryReset();
	return false;
}

constexpr std::array<Descriptor, static_cast<size_t>(DescriptorId::Count)> kDescriptors{{
    // All five share `ResetSettings`'s own heading and option list; only the action differs, which
    // is exactly what `ResetSettingsAction` selects in legacy.
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_flash},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_community_features},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_midi_follow},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_midi_devices},
    {sid(String::STRING_FOR_ARE_YOU_SURE_QMARK), kConfirmOptions, accept_reset_all},
}};

// Fix round 1, Minor 1. `render_oled` materialises `min(options.size(), kMaxOptions)` views while
// `step()` walks the descriptor's FULL count, so a descriptor with more options than the render
// arrays hold would let the cursor reach a row that is never drawn -- an invisible selection, and
// then an accept on it. The ceiling exists because this layer allocates nothing; this is the check
// that the ceiling is big enough for the table it guards.
static_assert(
    [] {
	    for (const Descriptor& d : kDescriptors) {
		    if (d.options.size() > kMaxOptions) {
			    return false;
		    }
	    }
	    return true;
    }(),
    "a descriptor carries more options than kMaxOptions: raise kMaxOptions, or render_oled will "
    "silently truncate a list step() can still walk");

} // namespace

const Descriptor& descriptor(DescriptorId id) {
	const size_t i = static_cast<size_t>(id);
	return kDescriptors[i < kDescriptors.size() ? i : 0];
}

Selection step(Selection s, int32_t offset, uint8_t count) {
	// `ContextMenu::selectEncoderAction`, statement for statement. The `do/while
	// (!isCurrentOptionAvailable())` loop collapses to one pass, because unavailable options are
	// not in the list at all (see `Descriptor::options`).
	if (count == 0) {
		return s;
	}
	const bool was_on_scroll_pos = s.option == s.scroll;
	const int32_t old_option = s.option;
	const int32_t next = old_option + offset;
	if (next >= count || next < 0) {
		// "currentOption = oldCurrentOption; return;" -- a turn off either end changes nothing at
		// all, not even the scroll, and does NOT clamp to the last option.
		return s;
	}

	Selection out{static_cast<uint8_t>(next), s.scroll};
	if (next < out.scroll) {
		out.scroll = static_cast<uint8_t>(next);
	}
	else if (offset >= 0 && !was_on_scroll_pos) {
		// The cursor had already left the top row, so the window follows it downwards and the
		// cursor stays on the *bottom* of the two rows. This is what `ui::oled::list`'s
		// centre-the-cursor rule would not do, and why this scene does not use that widget.
		out.scroll = static_cast<uint8_t>(old_option);
	}
	return out;
}

// ---------------------------------------------------------------------------------------------

void ContextMenuScene::open(const menu::MenuContext& ctx) {
	ctx_ = ctx;
}

bool ContextMenuScene::on_enter(const SceneId& id) {
	if (id.arg >= static_cast<uint16_t>(DescriptorId::Count)) {
		return false;
	}
	id_ = static_cast<DescriptorId>(id.arg);

	// `ContextMenu::setupAndCheckAvailability`: walk to the first available option, seed
	// `scrollPos` from it, and refuse to open when there is none. With availability expressed by
	// compaction (see `Descriptor::options`) the walk is "option 0, if there is one".
	if (descriptor(id_).options.empty()) {
		return false;
	}
	sel_ = Selection{};
	scroll_ = deluge::ui::ScrollText{};
	last_render_ms_ = 0;
	return true;
}

void ContextMenuScene::on_exit() {
	// Same fail-closed reasoning as `RenameScene::on_exit`: `open()` is the only writer of `ctx_`
	// and this scene is a singleton that outlives every visit, so a re-entry that forgot its
	// `open()` must not hand the accept hook the previous song's pointers. None of the five
	// shipped hooks reads the context, but the next descriptor to land will.
	ctx_ = menu::MenuContext{};
}

void ContextMenuScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	(void)role;
	last_render_ms_ = f.now_ms();

	const Descriptor& d = descriptor(id_);

	// The l10n ids resolved into views that stay alive for the call. `text_or_digits` only writes
	// its scratch when the profile carries no text hook, but the views must outlive the call
	// either way, so the scratches are locals here rather than temporaries.
	TextScratch heading_scratch;
	std::array<TextScratch, kMaxOptions> option_scratch{};
	std::array<std::string_view, kMaxOptions> options{};
	const size_t n = std::min(d.options.size(), kMaxOptions);
	for (size_t i = 0; i < n; i++) {
		options[i] = deluge_profile().text_or_digits(d.options[i], option_scratch[i]);
	}

	deluge::ui::oled::option_window(f, deluge_profile().text_or_digits(d.title, heading_scratch),
	                                std::span<const std::string_view>{options.data(), n}, sel_.option, sel_.scroll,
	                                scroll_);
}

void ContextMenuScene::leds(deluge::ui::LedFrame& f) {
	// `ContextMenu::focusRegained`: `indicator_leds::blinkLed(IndicatorLED::BACK)`, and nothing
	// else. The saving/loading variants also blink SAVE/LOAD and clear the other of the pair, but
	// no shipped descriptor is one of those.
	//
	// The period is `ui::kFlashTime` (`ui/core/metrics.h`), imported rather than re-spelled as a
	// literal 110: `indicator_leds::blinkLed` runs at exactly that constant
	// (`hid/led/indicator_leds.cpp:108`, `definitions_cxx.hpp:159`), and `rename_scene.cpp:44`
	// already imports it this way. (Fix round 1, Minor 3 -- this was a third hand-written copy.)
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back),
	            deluge::ui::LedState{deluge::ui::LedState::Kind::Blink, static_cast<uint16_t>(kFlashTime), 0});
}

Result ContextMenuScene::handle(const UiEvent& event, const UiContext& ctx) {
	(void)ctx;
	switch (event.kind) {
	case UiEvent::Kind::Button:
	case UiEvent::Kind::EncoderButton:
		// `ContextMenu::buttonAction`. Both arms are `on`-only; the `isUIModeWithinRange` gate and
		// the `inCardRoutine` retry are shell business (`Action::Kind::LockInput`), not the
		// scene's.
		if (!event.on) {
			break;
		}
		if (event.button == ButtonId::Back) {
			return close();
		}
		if (event.button == ButtonId::SelectEnc) {
			// `getAcceptButton()` is `SELECT_ENC` for the base class, and the `b == SELECT_ENC` arm
			// runs before it, so on every shipped descriptor the two are the same button and the
			// third arm of that if/else is dead. `ContextMenuForSaving`/`ContextMenuForLoading`
			// answer `SAVE`/`LOAD` instead; both are file-browser menus this plan does not port,
			// so `Descriptor` carries no `accept_button` field yet. Adding one means one extra
			// comparison here, nothing more.
			return accept_current_option();
		}
		break;

	case UiEvent::Kind::Encoder:
		if (event.encoder == deluge::ui::EncoderRole::Select) {
			// `ContextMenu::selectEncoderAction` -> `renderUIsForOled()`. `option_window` is
			// render-only (it claims nothing), so the turn arrives here rather than being consumed
			// during the render pass the way `list` consumes it for `MenuScene`.
			const Selection before = sel_;
			sel_ = step(sel_, event.delta, static_cast<uint8_t>(descriptor(id_).options.size()));
			Result r;
			r.handled = true;
			if (sel_ != before) {
				(void)r.actions.push_back(Action{.kind = Action::Kind::Repaint, .surface = SurfaceRole::Main});
			}
			return r;
		}
		break;

	case UiEvent::Kind::Pad:
		// `ContextMenu::padAction`: a press closes, and every pad event is `DEALT_WITH` -- so a
		// release is swallowed rather than falling through to the view underneath.
		if (event.on) {
			return close();
		}
		{
			Result r;
			r.handled = true;
			return r;
		}

	case UiEvent::Kind::AllReleased:
		break;
	}
	return Result::unhandled();
}

Result ContextMenuScene::accept_current_option() {
	Result r;
	r.handled = true;
	if (descriptor(id_).accept(ctx_, sel_.option)) {
		// "If returns false, will cause UI to exit" -- so a true keeps the menu open. No shipped
		// descriptor does, but the convention is legacy's and is what a multi-step menu would use.
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
		return r;
	}
	return close();
}

Result ContextMenuScene::close() {
	// `ContextMenu::close()`. `Remove` naming this scene's own kind, not `Pop`: the accept hooks
	// raise notices (`FlashStorage::factoryReset` and friends all `notify::notice(...)`), so an
	// overlay can legitimately be pushed above this scene between the decision and the router
	// applying it -- and `Pop` would take that down too (`shell/scene.h`'s note on
	// `Action::Kind::Pop`).
	Result r;
	r.handled = true;
	(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = kContextMenuSceneKind});
	return r;
}

std::optional<uint32_t> ContextMenuScene::next_timer() const {
	// The one thing on screen that moves without input is the focused row's side scroller, and it
	// moves only while its label is too wide for the box (`scroll_text`, which freezes and resets
	// otherwise). `ScrollText` records both widths, so "is it scrolling" is answerable here without
	// re-measuring anything.
	//
	// No shipped descriptor reaches it: "OK" is 18px in a 78px box. Kept anyway, because the
	// alternative is a scene whose first wide-labelled descriptor silently stops animating.
	if (scroll_.text_w_last <= scroll_.rect_w_last) {
		return std::nullopt;
	}
	const int32_t due = static_cast<int32_t>(scroll_.next_step_ms - last_render_ms_);
	return static_cast<uint32_t>(due > 0 ? due : 0);
}

ContextMenuScene& context_menu_scene() {
	static ContextMenuScene scene;
	return scene;
}

} // namespace deluge::scenes::context_menu
