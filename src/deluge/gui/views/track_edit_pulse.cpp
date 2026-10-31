/*
 * Copyright © 2026 Sean Ditny
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

#include "gui/views/track_edit_pulse.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/session/track_link.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/led/pad_leds.h"
#include "model/output.h"
#include <algorithm>

namespace deluge::gui::track_edit_pulse {

// Cache only visible pads during normal rendering, like the note editor's pulse
// overlay. Timer ticks never walk the song or re-render clip contents.
namespace {
struct PulsePad {
	RGB base;
	RGB blur;
};
PulsePad pulse_pads[kDisplayHeight][kDisplayWidth + kSideBarWidth];
uint32_t pulse_columns[kDisplayHeight]{};
uint32_t held_columns[kDisplayHeight]{};
UI* pulse_ui = nullptr;
int32_t menu_pulse_column = -1;
int32_t pulse_progress = 1;
bool pulse_increasing = true;
int32_t held_progress = 1;
bool held_increasing = true;
constexpr int32_t kPulseInterval = 50;
constexpr int32_t kPulseMaximum = (65535 / 100) * 60;
} // namespace

// A new hold starts a complete cycle without restarting other tracks' lock indicators.
// Clear old held targets immediately so a timer cannot repaint them before the next UI render.
void resetHeldPulse() {
	held_progress = 1;
	held_increasing = true;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		pulse_columns[y] &= ~held_columns[y];
		held_columns[y] = 0;
	}
}

void clearPulseRow(RGB* row, int32_t y, bool sidebar) {
	if (row != PadLEDs::image[y]) {
		return; // Transition / offscreen rendering must not replace the live cache.
	}
	if (pulse_ui != getRootUI()) {
		std::fill_n(pulse_columns, kDisplayHeight, 0);
		std::fill_n(held_columns, kDisplayHeight, 0);
		pulse_ui = getRootUI();
	}
	if (y == 0 && !sidebar) {
		menu_pulse_column = -1;
	}
	constexpr uint32_t main_columns = (1u << kDisplayWidth) - 1;
	pulse_columns[y] &= sidebar ? main_columns : ~main_columns;
	held_columns[y] &= sidebar ? main_columns : ~main_columns;
}

RGB renderPulse(RGB colour, Output const& output, RGB* row, int32_t x, int32_t y, bool held) {
	if (!track_link::enabled()) {
		return colour;
	}
	RGB track_colour = RGB::fromHue(output.colour);
	// Dim pad indicators pulse within their existing brightness range. Blurring
	// such low RGB values would quantize some hues to a constant colour. Held clips
	// still blend toward the full track colour to highlight the selection.
	PulsePad pad{colour.adjust(255, track_colour == deluge::gui::colours::red ? 4 : 3),
	             (held || colour == track_colour) ? track_colour.forBlur() : colour};
	if (row == PadLEDs::image[y]) {
		if (y == 0 && getRootUI() == &sessionView && track_link::isMenuOpen() && track_link::isSelected(&output)) {
			menu_pulse_column = x;
		}
		pulse_pads[y][x] = pad;
		pulse_columns[y] |= 1u << x;
		if (held) {
			held_columns[y] |= 1u << x;
		}
		else {
			held_columns[y] &= ~(1u << x);
		}
		if (!uiTimerManager.isTimerSet(TimerName::TRACK_LOCK_PULSE)) {
			uiTimerManager.setTimer(TimerName::TRACK_LOCK_PULSE, kPulseInterval);
		}
	}
	return RGB::blend(pad.base, pad.blur, 65536 - (held ? held_progress : pulse_progress));
}

void pulseTimer() {
	auto* root = getRootUI();
	if (!track_link::enabled() || root != pulse_ui || (root != &sessionView && root != &arrangerView)
	    || (root == &sessionView && !sessionView.gridTrackRowEnabled())) {
		return;
	}
	bool track_menu = root == &sessionView && getCurrentUI() == &soundEditor && track_link::isMenuOpen();
	bool main = false;
	bool sidebar = false;
	for (auto columns : pulse_columns) {
		main |= (columns & ((1u << kDisplayWidth) - 1)) != 0;
		sidebar |= (columns >> kDisplayWidth) != 0;
	}
	if (!main && !sidebar) {
		return;
	}
	uiTimerManager.setTimer(TimerName::TRACK_LOCK_PULSE, kPulseInterval);

	// Only the edited track pad may animate through its sound menu. Shortcut overlays
	// and other menus retain exclusive ownership of the pad image.
	if ((getCurrentUI() != root && !track_menu) || PadLEDs::renderingLock
	    || (track_menu && soundEditor.should_render_shortcut_overlay())
	    || (currentUIMode != UI_MODE_NONE && currentUIMode != UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION)) {
		return;
	}
	if (track_menu) {
		if (menu_pulse_column < 0) {
			return;
		}
		main = true;
		sidebar = false;
	}
	// The VU meter may update the sidebar directly between normal renders.
	if (view.displayVUMeter && view.getModKnobMode() == 0) {
		sidebar = false;
	}
	pulse_progress = std::clamp<int32_t>(pulse_progress + (pulse_increasing ? 3000 : -3000), 1, kPulseMaximum);
	if (pulse_progress == 1 || pulse_progress == kPulseMaximum) {
		pulse_increasing = !pulse_increasing;
	}
	held_progress = std::clamp<int32_t>(held_progress + (held_increasing ? 3000 : -3000), 1, kPulseMaximum);
	if (held_progress == 1 || held_progress == kPulseMaximum) {
		held_increasing = !held_increasing;
	}
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		uint32_t columns = pulse_columns[y];
		if (track_menu) {
			columns &= y == 0 ? (1u << menu_pulse_column) : 0u;
		}
		if (columns == 0u) {
			continue;
		}
		for (int32_t x = 0; x < kDisplayWidth + (sidebar ? kSideBarWidth : 0); ++x) {
			if ((columns & (1u << x)) != 0u) {
				auto const& pad = pulse_pads[y][x];
				int32_t progress = (held_columns[y] & (1u << x)) != 0u ? held_progress : pulse_progress;
				PadLEDs::image[y][x] = RGB::blend(pad.base, pad.blur, 65536 - progress);
			}
		}
	}
	if (main) {
		PadLEDs::sendOutMainPadColours();
	}
	if (sidebar) {
		PadLEDs::sendOutSidebarColours();
	}
}

} // namespace deluge::gui::track_edit_pulse
