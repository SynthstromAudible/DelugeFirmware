/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "util/misc.h"
#include <array>
#include <cstdint>
enum class TimerName {
	DISPLAY,
	MIDI_LEARN_FLASH,
	DEFAULT_ROOT_NOTE,
	TAP_TEMPO_SWITCH_OFF,
	PLAY_ENABLE_FLASH,
	LED_BLINK,
	LED_BLINK_TYPE_1,
	LEVEL_INDICATOR_BLINK,
	SHORTCUT_BLINK,
	MATRIX_DRIVER,
	UI_SPECIFIC,
	DISPLAY_AUTOMATION,
	READ_INPUTS,
	BATT_LED_BLINK,
	GRAPHICS_ROUTINE,
	OLED_LOW_LEVEL,
	OLED_CONSOLE,
	OLED_SCROLLING_AND_BLINKING,
	SYSEX_DISPLAY,
	METER_INDICATOR_BLINK,
	SEND_MIDI_FEEDBACK_FOR_AUTOMATION,
	INTERPOLATION_SHORTCUT_BLINK,
	PAD_SELECTION_SHORTCUT_BLINK,
	/// Total number of timers
	NUM_TIMERS
};

struct Timer {
	bool active;
	uint32_t triggerTime;
};

class UITimerManager {
public:
	UITimerManager();

	void routine();
	void setTimer(TimerName which, int32_t ms);
	void setTimerSamples(TimerName which, int32_t samples);
	void unsetTimer(TimerName which);

	bool isTimerSet(TimerName which);
	void setTimerByOtherTimer(TimerName which, TimerName fromTimer);

private:
	uint32_t timeNextEvent;
	std::array<Timer, util::to_underlying(TimerName::NUM_TIMERS)> timers_;
	void workOutNextEventTime();

public:
	[[gnu::always_inline]] inline Timer& getTimer(TimerName which) { return timers_[util::to_underlying(which)]; }
};

extern UITimerManager uiTimerManager;
