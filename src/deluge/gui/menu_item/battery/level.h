/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/menu_item.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "model/settings/runtime_feature_settings.h"
#include <cstdio>
// External battery voltage variable from deluge.cpp
extern uint16_t batteryMV;

namespace deluge::gui::menu_item::battery {
class Level final : public MenuItem {
public:
	using MenuItem::MenuItem;

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::ShowBatteryLevel);
	}
	void drawPixelsForOled() override {
		deluge::hid::display::oled_canvas::Canvas& canvas = hid::display::OLED::main;
		char buffer[50];
		getBatteryString(buffer);
		canvas.drawStringCentredShrinkIfNecessary(buffer, 22, 18, 20);
	}

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		// Store initial voltage to detect charging
		lastBatteryMV = batteryMV;
		voltageCheckCounter = 0;

		drawValue();
		// Start the timer for updates
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
	}

	void drawValue() {
		char buffer[50];
		getBatteryString(buffer);
		display->setScrollingText(buffer);
	}

	ActionResult timerCallback() override {
		// Check if voltage is rising (charging detection)
		voltageCheckCounter++;
		if (voltageCheckCounter >= 4) { // Check every 2 seconds
			int32_t voltageDiff = batteryMV - lastBatteryMV;
			// Consider it charging if voltage rose by more than 5mV in 2 seconds
			// This threshold may need tuning based on real hardware behavior
			isCharging = (voltageDiff > 5);
			lastBatteryMV = batteryMV;
			voltageCheckCounter = 0;
		}

		drawValue();
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
		return ActionResult::DEALT_WITH;
	}

private:
	uint16_t lastBatteryMV = 0;
	uint8_t voltageCheckCounter = 0;
	bool isCharging = false;

	void getBatteryString(char* buffer) {
		// Calculate battery percentage
		// Assuming battery range is 2600mV (0%) to 4200mV (100%)
		const uint16_t minVoltage = 2600;
		const uint16_t maxVoltage = 4200;

		int32_t percentage = 0;
		if (batteryMV > minVoltage) {
			percentage = ((int32_t)(batteryMV - minVoltage) * 100) / (maxVoltage - minVoltage);
			if (percentage > 100)
				percentage = 100;
			if (percentage < 0)
				percentage = 0;
		}

		// Determine status
		const char* status = "";
		if (percentage >= 99) {
			status = " FULL";
		}
		else if (isCharging) {
			status = " CHG"; // Charging indicator
		}

		// Format the string with percentage, voltage, and status
		sprintf(buffer, "%d%% (%dmV)%s", percentage, batteryMV, status);
	}
};
} // namespace deluge::gui::menu_item::battery
