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

#include "gui/ui/root_knobs.h"

#include "board_controls.hpp"
#include "ui/controls/board.h"

#include "base/notify/notice_queue.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/shell_guest_knobs.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/performance_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "model/project_bridge/param_ids.h"
#include "ui/scenes/services/knob_presentation.h"
#include "ui/scenes/services/mod_knobs.h"
#include "util/functions.h"

#include <cstdint>
#include <string>
#include <variant>

namespace deluge::gui::ui::root_knobs {
namespace {

using deluge::ui::scenes::services::KnobPresentation;
using deluge::ui::scenes::services::ModKnobs;
using deluge::ui::scenes::services::ModTurn;

/// @brief The gold knobs as the root views show them: the legacy display, `indicator_leds` and `View`, each method
///        the `View` code it stands for.
class RootKnobPresentation final : public KnobPresentation {
	/// @brief The turned param as the performance view and `View`'s popup number it.
	static deluge::bridge::RuntimeParam runtime_of(const ModTurn& turn) {
		return deluge::bridge::runtimeParamOf(turn.param.plugin, turn.param.target)
		    .value_or(deluge::bridge::RuntimeParam{});
	}

public:
	/// @brief Performance view showing this param itself (which draws its display here), or a song load in flight
	///        (main's `View::modEncoderAction_existentParam`, `origin/main:src/deluge/gui/views/view.cpp`).
	bool suppress_value_popup(const ModTurn& turn) override {
		const deluge::bridge::RuntimeParam runtime = runtime_of(turn);
		const bool performance_shows_it =
		    getRootUI() == &performanceView
		    && performanceView.possiblyRefreshPerformanceViewDisplay(runtime.kind, runtime.index, turn.new_knob_pos);
		return performance_shows_it || loadSongUI.isLoadingSong();
	}
	void show_value_popup(const ModTurn& turn, PatchSource source1, PatchSource source2,
	                      const deluge::ui::scenes::overlay::PopupContext& /*context*/, uint32_t /*now_ms*/) override {
		const deluge::bridge::RuntimeParam runtime = runtime_of(turn);
		view.displayModEncoderValuePopup(runtime.kind, runtime.index, turn.new_knob_pos, source1, source2);
	}
	void value_written() override { view.refreshKnobParamManager(); }
	void blink_indicator(int32_t knob, bool bipolar) override { indicator_leds::blinkKnobIndicator(knob, bipolar); }
	void stop_blink_indicator(int32_t knob) override { indicator_leds::stopBlinkingKnobIndicator(knob); }
	void indicators_changed() override { view.setKnobIndicatorLevels(); }
	void mode_changed() override {
		view.setKnobIndicatorLevels();
		view.setModLedStates();
	}
	/// @brief Only the sidebar, and only while the VU meter is drawn there (`View::modButtonAction`).
	void vu_sidebar_changed() override {
		if (view.vuMeterRendered()) {
			uiNeedsRendering(getRootUI(), 0);
		}
	}
	/// @brief The main pads of a timeline view (main's `View::modEncoderAction_existentParam`,
	///        `origin/main:src/deluge/gui/views/view.cpp`).
	void note_tails_changed() override {
		if (getRootUI() != nullptr && getRootUI()->toTimelineView() != nullptr) {
			uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
		}
	}
	/// @brief A mod button's popup, or a push's with its button held (main's
	/// `ModControllableAudio::displayFilterSettings`
	///        and its siblings): up until the release cancels it.
	void popup_text(std::string_view text) override { display->popupText(std::string{text}.c_str()); }
	/// @brief A push's setting change (`Sound::modEncoderButtonAction`'s `deluge::base::notify::notice`).
	void notice_text(std::string_view text) override { deluge::base::notify::notice_text(text); }
	/// @brief The song compressor's setting (main's `GlobalEffectable::modEncoderActionForNonExistentParam`).
	void notification(std::string_view title, std::string_view value) override {
		display->displayNotification(title, value);
	}
	/// @brief The song compressor knob's new position (main's `GlobalEffectable::modEncoderActionForNonExistentParam`'s
	///        `indicator_leds::setKnobIndicatorLevel`).
	void setting_indicator(int32_t knob, int32_t knob_pos) override {
		// Legacy hands its `int` level to the `uint8_t` parameter as it is; 0..128 fits.
		indicator_leds::setKnobIndicatorLevel(static_cast<uint8_t>(knob),
		                                      static_cast<uint8_t>(knob_pos + kKnobPosOffset));
	}
	void popup(deluge::l10n::String string) override { display->displayPopup(deluge::l10n::get(string)); }
	void cancel_popup() override { display->cancelPopup(); }
};

/// The one presentation the root views show through.
RootKnobPresentation root_presentation;

/// @brief The shared knobs, told the song and the root view now, or null when the focus is not in the current song
///        (`View::knobsOnCurrentSong`: such knobs point at nothing).
/// @note `refresh_indicators` runs this from inside `ModKnobs::push` and `ModKnobs::section_button`, through the
///       presentation's `indicators_changed` and `mode_changed`. Those are each gesture's last statement, and the
///       state this re-tells is what the service already holds, so nothing in the gesture reads a changed value.
ModKnobs* knobs_now() {
	refreshKnobsForRoot();
	return view.knobsOnCurrentSong() ? &sharedModKnobs() : nullptr;
}

} // namespace

void turn(int32_t knob, int32_t offset) {
	if (knob < 0 || knob > UINT8_MAX) {
		return;
	}
	const std::optional<deluge::ui::controls::Logical> slot = deluge::ui::controls::map_input(
	    sharedModKnobs().board(), {deluge::ui::controls::Physical::Kind::SLOT_ENCODER, static_cast<uint8_t>(knob)});
	if (!slot) {
		return;
	}
	if (ModKnobs* knobs = knobs_now()) {
		knobs->turn(slot->index, offset, shellHostUI.nowMs(), root_presentation);
	}
}

void push(uint8_t knob, bool on) {
	const std::optional<deluge::ui::controls::Logical> slot = deluge::ui::controls::map_input(
	    sharedModKnobs().board(), {deluge::ui::controls::Physical::Kind::SLOT_PRESS, knob});
	if (!slot) {
		return;
	}
	ModKnobs* knobs = knobs_now();
	if (knobs == nullptr) {
		return;
	}
	const int32_t mode = knobs->mode();
	const bool mode_held =
	    mode >= 0 && Buttons::isButtonPressed(deluge::hid::button::fromXY(modButtonX[mode], modButtonY[mode]));
	knobs->push(slot->index, on,
	            {.shift = Buttons::isShiftButtonPressed(),
	             .learn = Buttons::isButtonPressed(deluge::hid::button::LEARN),
	             .mode_held = mode_held},
	            root_presentation);
}

void mod_button(uint8_t button, bool on) {
	const std::optional<deluge::ui::controls::Logical> section = deluge::ui::controls::map_input(
	    sharedModKnobs().board(), {deluge::ui::controls::Physical::Kind::SECTION_BUTTON, button});
	if (!section) {
		return;
	}
	if (ModKnobs* knobs = knobs_now()) {
		knobs->section_button(section->index, on, root_presentation);
	}
}

void refresh_indicators() {
	ModKnobs* knobs = knobs_now();
	if (knobs == nullptr || std::holds_alternative<std::monostate>(knobs->focus().owner)) {
		indicator_leds::clearKnobIndicatorLevels();
		return;
	}
	const bool stuttering = isUIModeActive(UI_MODE_STUTTERING);
	for (int32_t k = 0; k < knobs->board().slots; k++) {
		if (indicator_leds::isKnobIndicatorBlinking(k)) {
			continue;
		}
		const ModKnobs::IndicatorLevel level = ModKnobs::indicator_level(knobs->indicator_info(k), stuttering);
		if (level.declared) {
			indicator_leds::setKnobIndicatorLevel(static_cast<uint8_t>(k), level.level, level.bipolar);
		}
	}
}

std::optional<deluge::project::ParamAddress> knob_address(int32_t knob) {
	const ModKnobs* knobs = knobs_now();
	if (knobs == nullptr) {
		return std::nullopt;
	}
	const std::optional<deluge::project::ParamAddress> address = knobs->knob_address(knob);
	// An engine param its owner always holds, or a cable a paste makes; a MIDI lane only once it is made
	// (the `autoParam` test of main's `View::modEncoderAction`, `origin/main:src/deluge/gui/views/view.cpp`).
	if (address && address->space != deluge::project::ParamSpace::ENGINE
	    && !deluge::gui::shell_host::host_ports().controls->param_value(*address)) {
		return std::nullopt;
	}
	return address;
}

} // namespace deluge::gui::ui::root_knobs
