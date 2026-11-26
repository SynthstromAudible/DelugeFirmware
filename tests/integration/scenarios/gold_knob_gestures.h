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

#pragma once

/// @file
/// @brief The gold knobs' own service, driven by a scenario with nothing drawn: a turn, a push or a mod button writes
///        the song as the app's knobs do, and the scenario reads the song.

#include "board_controls.hpp"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/overlay/param_popup_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/services/knob_presentation.h"
#include "ui/scenes/services/mod_knobs.h"
#include "ui/shell/input_lowering.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene_stack.h"

#include <cstdint>
#include <string_view>

namespace deluge::scenarios {

/// @brief A presentation that shows nothing.
class NoKnobPresentation final : public deluge::ui::scenes::services::KnobPresentation {
public:
	bool suppress_value_popup(const deluge::ui::scenes::services::ModTurn& /*turn*/) override { return false; }
	void show_value_popup(const deluge::ui::scenes::services::ModTurn& /*turn*/, PatchSource /*source1*/,
	                      PatchSource /*source2*/, const deluge::ui::scenes::overlay::PopupContext& /*context*/,
	                      uint32_t /*now_ms*/) override {}
	void value_written() override {}
	void blink_indicator(int32_t /*knob*/, bool /*bipolar*/) override {}
	void stop_blink_indicator(int32_t /*knob*/) override {}
	void indicators_changed() override {}
	void mode_changed() override {}
	void vu_sidebar_changed() override {}
	void note_tails_changed() override {}
	void popup_text(std::string_view /*text*/) override {}
	void notice_text(std::string_view /*text*/) override {}
	void notification(std::string_view /*title*/, std::string_view /*value*/) override {}
	void setting_indicator(int32_t /*knob*/, int32_t /*knob_pos*/) override {}
	void popup(deluge::l10n::String /*string*/) override {}
	void cancel_popup() override {}
};

/// @brief The shared knobs over a scene stack of their own, pointed at one song and one owner, and what they show
///        dropped.
class GoldKnobGestures {
public:
	/// @param song  The song the knobs act on (also the current song while they do).
	/// @param focus The owner the knobs point at.
	GoldKnobGestures(Song& song, const deluge::ui::controls::KnobFocus& focus)
	    : stack_(registry_),
	      knobs_(deluge::ui::scenes::overlay::param_popup_scene(), stack_, lowering_, kDelugeControls) {
		knobs_.set_ports(&deluge::gui::shell_host::host_ports());
		knobs_.set_model(&song.project);
		knobs_.set_focus(focus, false);
	}
	GoldKnobGestures(const GoldKnobGestures&) = delete;
	GoldKnobGestures& operator=(const GoldKnobGestures&) = delete;

	/// @brief A gold-knob turn.
	/// @param knob   0 for the lower knob, 1 for the upper.
	/// @param offset The detents.
	void turn(int32_t knob, int32_t offset) { knobs_.turn(knob, offset, now_ms_ += 10, out_); }
	/// @brief A gold-knob push, no button held.
	/// @param knob 0 for the lower knob, 1 for the upper.
	/// @param on   Press or release.
	void push(int32_t knob, bool on) { knobs_.push(knob, on, {}, out_); }

private:
	deluge::ui::shell::SceneRegistry registry_ = deluge::ui::scenes::scene_registry();
	deluge::ui::shell::SceneStack stack_;
	deluge::ui::shell::InputLowering lowering_{deluge::ui::shell::deluge_profile(), {}};
	deluge::ui::scenes::services::ModKnobs knobs_;
	NoKnobPresentation out_;
	uint32_t now_ms_ = 1000;
};

} // namespace deluge::scenarios
