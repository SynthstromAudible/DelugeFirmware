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

#include <cstdint>
#define TIMER_DISPLAY 0
#define TIMER_MIDI_LEARN_FLASH 1
#define TIMER_DEFAULT_ROOT_NOTE 2
#define TIMER_TAP_TEMPO_SWITCH_OFF 3
#define TIMER_PLAY_ENABLE_FLASH 4
#define TIMER_LED_BLINK 5
#define TIMER_LED_BLINK_TYPE_1 6
#define TIMER_LEVEL_INDICATOR_BLINK 7
#define TIMER_SHORTCUT_BLINK 8
#define TIMER_MATRIX_DRIVER 9
#define TIMER_UI_SPECIFIC 10
#define TIMER_DISPLAY_AUTOMATION 11
#define TIMER_READ_INPUTS 12
#define TIMER_BATT_LED_BLINK 13
#define TIMER_GRAPHICS_ROUTINE 14
#define TIMER_OLED_LOW_LEVEL 15
#define TIMER_OLED_CONSOLE 16
#define TIMER_OLED_SCROLLING_AND_BLINKING 17
#define TIMER_SYSEX_DISPLAY 18
#define TIMER_METER_INDICATOR_BLINK 19
#define TIMER_SEND_MIDI_FEEDBACK_FOR_AUTOMATION 20
#define TIMER_INTERPOLATION_SHORTCUT_BLINK 21
#define NUM_TIMERS 22

struct Timer {
	bool active;
	uint32_t triggerTime;
};

class UITimerManager {
public:
	UITimerManager();

	void routine();
	void setTimer(int32_t i, int32_t ms);
	void setTimerSamples(int32_t i, int32_t samples);
	void unsetTimer(int32_t i);

	bool isTimerSet(int32_t i);
	void setTimerByOtherTimer(int32_t i, int32_t j);

	Timer timers[NUM_TIMERS];

private:
	uint32_t timeNextEvent;
	void workOutNextEventTime();
};

extern UITimerManager uiTimerManager;
