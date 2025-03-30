/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

#include "power_manager.h"
#include "OSLikeStuff/scheduler_api.h"
#include "RZA1/system/iodefine.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "io/debug/log.h"
#include "util/functions.h"
#include <algorithm>

extern "C" {
#include "RZA1/gpio/gpio.h"
}

// int32_t voltageReadingLastTime = 65535 * 3300;
// uint8_t batteryCurrentRegion = 2;
// uint16_t batteryMV;
// ;

// if (batteryCurrentRegion == 0) {
// 			if (batteryMV > 2950) {
// makeBattLEDSolid:
// 				batteryCurrentRegion = 1;
// 				setOutputState(BATTERY_LED.port, BATTERY_LED.pin, false);
// 				uiTimerManager.unsetTimer(TimerName::BATT_LED_BLINK);
// 			}
// 		}
// 		else if (batteryCurrentRegion == 1) {
// 			if (batteryMV < 2900) {
// 				batteryCurrentRegion = 0;
// 				batteryLEDBlink();
// 			}

// 			else if (batteryMV > 3300) {
// 				batteryCurrentRegion = 2;
// 				setOutputState(BATTERY_LED.port, BATTERY_LED.pin, true);
// 				uiTimerManager.unsetTimer(TimerName::x);
// 			}
// 		}
// 		else {
// 			if (batteryMV < 3200) {
// 				goto makeBattLEDSolid;
// 			}
// 		}

namespace deluge::system {

// Global instance
PowerManager powerManager;

PowerManager::PowerManager() {

	// Initialize ADC for battery voltage reading
	ADC.ADCSR = (1 << 15) |         // Start conversion
	            (1 << 13) |         // Enable ADC
	            (0b011 << 6) |      // Clock select
	            SYS_VOLT_SENSE_PIN; // Channel select // Initialize with a reasonable value
}

void PowerManager::update() {
	static uint32_t lastUpdateTime = 0;
	double currentTime = getSystemTime();

	// Remove the 10-second delay for testing
	// if (currentTime - lastUpdateTime < 10000) {
	//     return;
	// }
	lastUpdateTime = currentTime;

	batteryMV = getVoltageReading();

	// Update battery LED based on power status
	if (isExternalPowerConnected()) {
		// When on external power, LED should be OFF
		setOutputState(BATTERY_LED.port, BATTERY_LED.pin, true); // HIGH = OFF for open-drain
		uiTimerManager.unsetTimer(TimerName::BATT_LED_BLINK);
	}
	else {
		// On battery, LED behavior depends on battery level
		BatteryStatus status = getBatteryStatus();
		switch (status) {
		case BatteryStatus::FULL:
		case BatteryStatus::HEALTHY:
			// LED OFF when battery healthy
			setOutputState(BATTERY_LED.port, BATTERY_LED.pin, true); // HIGH = OFF for open-drain
			uiTimerManager.unsetTimer(TimerName::BATT_LED_BLINK);
			break;
		case BatteryStatus::WARNING:
			// Solid RED LED for low battery
			setOutputState(BATTERY_LED.port, BATTERY_LED.pin, false); // LOW = ON for open-drain
			uiTimerManager.unsetTimer(TimerName::BATT_LED_BLINK);
			break;
		case BatteryStatus::CRITICAL:
			// Blinking RED LED for critical battery
			if (!uiTimerManager.isTimerSet(TimerName::BATT_LED_BLINK)) {
				batteryLEDBlink();
			}
			break;
		}

		// Set up for next analog read
		ADC.ADCSR = (1 << 13) | (0b011 << 6) | SYS_VOLT_SENSE_PIN;
	}
}

int32_t PowerManager::getVoltageReading() const {
	if (ADC.ADCSR & (1 << 15)) {
		int batteryMV;
		int32_t numericReading = ADC.ADDRF;
		// Apply LPF
		int32_t voltageReading = numericReading * 3300;
		int32_t distanceToGo = voltageReading - voltageReadingLastTime;
		voltageReadingLastTime += distanceToGo >> 4;

		// We only >> by 15 so that we intentionally double the value, because the incoming voltage is halved by a
		// resistive divider already
		batteryMV = (voltageReadingLastTime) >> 15;
		// D_PRINT("batt mV: ");
		// D_PRINTLN(batteryMV);
		return batteryMV;
	}

	return 0;
}

int32_t PowerManager::getStableVoltage() const {
	return batteryMV;
}

PowerSource PowerManager::getPowerSource() const {
	return currentPowerSource;
}

bool PowerManager::isExternalPowerConnected() const {
	return currentPowerSource == PowerSource::USB;
}

BatteryStatus PowerManager::getBatteryStatus() const {
	if (batteryMV <= VOLTAGE_CRITICAL) {
		return BatteryStatus::CRITICAL;
	}
	else if (batteryMV <= VOLTAGE_LOW) {
		return BatteryStatus::WARNING;
	}
	else if (batteryMV > VOLTAGE_LOW && batteryMV <= VOLTAGE_FULL) {
		return BatteryStatus::HEALTHY;
	}
	else if (batteryMV > VOLTAGE_FULL) {
		return BatteryStatus::FULL;
	}

	return BatteryStatus::CRITICAL;
}

int8_t PowerManager::getBatteryChargePercentage() const {
	if (batteryMV >= VOLTAGE_FULL)
		return 100;
	if (batteryMV <= VOLTAGE_CRITICAL)
		return 0;

	// Non-linear mapping that better represents Li-ion discharge curve
	// Gives more resolution in the middle range where voltage drops fastest
	const float normalized = (batteryMV - VOLTAGE_CRITICAL) / static_cast<float>(VOLTAGE_FULL - VOLTAGE_CRITICAL);
	return static_cast<int8_t>(normalized * normalized * 100);
}

void PowerManager::displayPowerStatus() {
	auto& powerManager = deluge::system::powerManager;

	// Get fresh reading
	powerManager.update();

	// Format the battery status text
	char text[32];
	if (powerManager.isExternalPowerConnected()) {
		strcpy(text, "USB (");
		intToString(powerManager.getStableVoltage(), text + strlen(text));
		strcat(text, "mV)");
	}
	else {
		intToString(powerManager.getBatteryChargePercentage(), text);
		strcat(text, "% (");
		intToString(powerManager.getStableVoltage(), text + strlen(text));
		strcat(text, "mV)");
	}

	display->displayPopup(text);
}

void PowerManager::batteryLEDBlink() {
	bool batteryLEDState = false; // Start with LED ON (LOW for open-drain)
	setOutputState(BATTERY_LED.port, BATTERY_LED.pin, batteryLEDState);
	int32_t blinkPeriod = ((int32_t)batteryMV - 2630) * 3;
	blinkPeriod = std::min(blinkPeriod, 500_i32);
	blinkPeriod = std::max(blinkPeriod, 60_i32);
	uiTimerManager.setTimer(TimerName::BATT_LED_BLINK, blinkPeriod);
	batteryLEDState = !batteryLEDState;
}

} // namespace deluge::system
