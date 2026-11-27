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
/// @brief `GuestKnobPresentation`. See `guest_knob_presentation.h`.

#include "ui/scenes/services/guest_knob_presentation.h"

#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/registry.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"

#include <algorithm>
#include <utility>

namespace deluge::ui::scenes::services {

using deluge::synth::PatchSource;

using deluge::ui::shell::Action;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;

bool GuestKnobPresentation::suppress_value_popup(const ModTurn& turn) {
	// "let's see if we're editing the same param in the menu, if so, don't show pop-up", in
	// `View::modEncoderAction_existentParam` (`origin/main:src/deluge/gui/views/view.cpp`). `SoundEditor`'s port is
	// `MenuScene`, so legacy's `getCurrentUI() == &soundEditor` is this identity test against the one menu interpreter
	// (`ui/scenes/registry.h`'s `menu_scene()`) -- not a `kind()` comparison, which would only *infer* that the scene
	// on top is that object.
	//
	// The other two arms (performance view, the song browser) are the root views', whose presentation answers them.
	if (&stack_.input_top() != static_cast<const deluge::ui::shell::Scene*>(&menu_scene())) {
		return false;
	}
	// `focused_value()`, not `focused()`: legacy's same-param test forwards through a horizontal page to the focused
	// value column (`Submenu::shouldForwardButtons` (`origin/main:src/deluge/gui/menu_item/submenu.cpp`)) -- the page
	// node itself has no `bind.param` to compare against, so `focused()` alone would never suppress the popup on a
	// horizontal menu at all.
	const menu::Node* focused = menu_scene().focused_value();
	if (focused == nullptr || !focused->bind.param.valid()) {
		return false;
	}
	const menu::AutomationParam pair = menu::legacy_pair_of(focused->bind.param);
	return pair.kind == turn.kind && pair.id == turn.param_id;
}

void GuestKnobPresentation::show_value_popup(const ModTurn& turn, PatchSource source1, PatchSource source2,
                                             const overlay::PopupContext& context, uint32_t now_ms) {
	// The popup's `show` reports whether it redrew; a second detent on an open popup moves its text, and only the
	// caller's `Repaint` draws it.
	const bool committed = popup_.show(turn.kind, turn.param_id, turn.new_knob_pos, source1, source2, now_ms, context);
	// The popup is a scene: pushed when not stacked yet (it is input-transparent, so one may sit above it).
	const bool stacked = std::ranges::any_of(stack_.scenes(), [](const deluge::ui::shell::Scene* scene) {
		return scene->kind() == overlay::kParamPopupSceneKind;
	});
	if (!stacked) {
		(void)stack_.push(SceneId{.kind = overlay::kParamPopupSceneKind});
	}
	if (committed) {
		(void)result_.actions.push_back(
		    Action{.kind = Action::Kind::Repaint, .surface = deluge::ui::shell::SurfaceRole::Main});
	}
}

void GuestKnobPresentation::blink_indicator(int32_t knob, bool bipolar) {
	if (!in_range(knob)) {
		return;
	}
	// One indicator blinks at a time (`indicator_leds.cpp`'s `whichLevelIndicatorBlinking`): starting this knob's
	// blink ends the other's, which then shows its level again.
	blink_.fill(false);
	blink_start_ms_.fill(0);
	blink_[knob] = true;
	blink_bipolar_[knob] = bipolar;
	// `blinkKnobIndicator` restarts the flash from the top on every qualifying turn
	// (`indicator_leds::blinkKnobIndicator` re-seeds `levelIndicatorBlinksLeft`), so the phase origin moves with it.
	// `ModKnobs::leds` declares it.
	blink_start_ms_[knob] = now_ms_;
}

void GuestKnobPresentation::stop_blink_indicator(int32_t knob) {
	if (!in_range(knob)) {
		return;
	}
	blink_[knob] = false; // `stopBlinkingKnobIndicator`
	blink_start_ms_[knob] = 0;
}

void GuestKnobPresentation::note_tails_changed() {
	if (root_is_timeline_view_) {
		deluge::base::notify::raise(deluge::base::notify::Dirty::GridMain);
	}
	(void)result_.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
}

void GuestKnobPresentation::popup_text(std::string_view text) {
	deluge::base::notify::Notice n;
	n.kind = deluge::base::notify::Notice::Kind::Text;
	n.severity = deluge::base::notify::Notice::Severity::Warn; // stays until something removes it
	const size_t length = text.copy(n.text, sizeof(n.text) - 1);
	n.text[length] = '\0';
	(void)deluge::base::notify::notices().push(n);
}

void GuestKnobPresentation::notice_text(std::string_view text) {
	deluge::base::notify::notice_text(text);
}

void GuestKnobPresentation::notification(std::string_view title, std::string_view value) {
	deluge::base::notify::notice_notification(title, value);
}

void GuestKnobPresentation::popup(deluge::l10n::String string) {
	(void)result_.actions.push_back(Action{.kind = Action::Kind::Popup,
	                                       .surface = deluge::ui::shell::SurfaceRole::Main,
	                                       .popup_string = static_cast<uint16_t>(string)});
}

void GuestKnobPresentation::cancel_popup() {
	deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelPopup);
}

Result GuestKnobPresentation::take_result() {
	return std::exchange(result_, Result{});
}

} // namespace deluge::ui::scenes::services
