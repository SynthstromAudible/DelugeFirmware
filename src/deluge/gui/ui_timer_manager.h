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

#include "definitions_cxx.hpp"
#include "runtime/app_loop.h"
#include "util/misc.h"
#include <algorithm>
#include <array>
#include <cstdint>

/// How long the "ui routine" job waits before it runs again, in µs, when the next timer is
/// @p remainingSamples away on the render's sample clock (zero or less: already due).
///
/// At least 1 ms: a timer that asked to be reminded keeps its past trigger time, and without a
/// floor the job would spin on it. At most 100 ms: the sample clock runs ahead of the loop's clock
/// during an offline export and stops when the render does, so a long wait is re-read against it.
constexpr deluge::runtime::Micros uiTimerRunDelayUs(int64_t remainingSamples) {
	constexpr int64_t kMinUs = 1'000;
	constexpr int64_t kMaxUs = 100'000;
	return static_cast<deluge::runtime::Micros>(
	    std::clamp<int64_t>(remainingSamples * 1'000'000 / kSampleRate, kMinUs, kMaxUs));
}

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
	BACK_MENU_EXIT,
	DISPLAY_AUTOMATION,
	READ_INPUTS,
	BATT_LED_BLINK,
	GRAPHICS_ROUTINE,
	OLED_CONSOLE,
	OLED_SCROLLING_AND_BLINKING,
	SYSEX_DISPLAY,
	METER_INDICATOR_BLINK,
	SEND_MIDI_FEEDBACK_FOR_AUTOMATION,
	INTERPOLATION_SHORTCUT_BLINK,
	PAD_SELECTION_SHORTCUT_BLINK,
	NOTE_ROW_BLINK,
	LOADING_ANIMATION,
	MOD_ENCODER_POPUP_FLUSH,
	SELECTED_CLIP_PULSE,
	SELECTED_NOTE_PULSE,
	/// Idle countdown before the screensaver appears, then its frame clock while it shows
	SCREENSAVER,
	/// Total number of timers
	NUM_TIMERS
};

struct Timer {
	bool active{};
	uint32_t triggerTime{};
};

class UITimerManager {
public:
	UITimerManager();

	void routine();
	void setTimer(TimerName which, int32_t ms);
	void setTimerSamples(TimerName which, int32_t samples);
	void unsetTimer(TimerName which);

	bool isTimerSet(TimerName which);

	/// The "ui routine" job, which runs routine() at the next timer's time.
	deluge::runtime::JobId jobId = deluge::runtime::kNoJob;

private:
	uint32_t timeNextEvent{};
	std::array<Timer, util::to_underlying(TimerName::NUM_TIMERS)> timers_{};
	void workOutNextEventTime();
	/// Asks for the job's next run at the next timer's time; from another tier, for a run as soon as possible.
	void requestRun();

public:
	[[gnu::always_inline]] inline Timer& getTimer(TimerName which) { return timers_[util::to_underlying(which)]; }
};

extern UITimerManager uiTimerManager;
