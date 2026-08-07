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
PowerManager power_manager;

PowerManager::PowerManager() {

	// Initialize ADC for battery voltage reading
	ADC.ADCSR = (1 << 15) |         // Start conversion
	            (1 << 13) |         // Enable ADC
	            (0b011 << 6) |      // Clock select
	            SYS_VOLT_SENSE_PIN; // Channel select // Initialize with a reasonable value
}

void PowerManager::update() {
	battery_mv_ = getVoltageReading();

	// Update battery LED based on power status
	if (isExternalPowerConnected()) {
		// When on external power, LED should be OFF
		setOutputState(BATTERY_LED.port, BATTERY_LED.pin, true); // HIGH = OFF for open-drain
		uiTimerManager.unsetTimer(TimerName::BATT_LED_BLINK);
	}
	else {
		// On battery, LED behavior depends on battery level
		const BatteryStatus status = getBatteryStatus();
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
				batteryLedBlink();
			}
			break;
		}

		// Set up for next analog read
		ADC.ADCSR = (1 << 13) | (0b011 << 6) | SYS_VOLT_SENSE_PIN;
	}
}

int32_t PowerManager::getVoltageReading() const {
	if (ADC.ADCSR & (1 << 15)) {
		const int32_t numeric_reading = ADC.ADDRF;
		// Apply LPF
		const int32_t voltage_reading = numeric_reading * 3300;
		const int32_t distance_to_go = voltage_reading - voltage_reading_last_time_;
		voltage_reading_last_time_ += distance_to_go >> 4;

		// We only >> by 15 so that we intentionally double the value, because the incoming voltage is halved by a
		// resistive divider already
		// D_PRINT("batt mV: ");
		// D_PRINTLN(voltage_reading_last_time_ >> 15);
		return voltage_reading_last_time_ >> 15;
	}

	return 0;
}

int32_t PowerManager::getStableVoltage() const {
	return battery_mv_;
}

PowerSource PowerManager::getPowerSource() const {
	return current_power_source_;
}

bool PowerManager::isExternalPowerConnected() const {
	return current_power_source_ == PowerSource::USB;
}

BatteryStatus PowerManager::getBatteryStatus() const {
	if (battery_mv_ <= voltage_critical_) {
		return BatteryStatus::CRITICAL;
	}
	else if (battery_mv_ <= voltage_low_) {
		return BatteryStatus::WARNING;
	}
	else if (battery_mv_ > voltage_low_ && battery_mv_ <= voltage_full_) {
		return BatteryStatus::HEALTHY;
	}
	else if (battery_mv_ > voltage_full_) {
		return BatteryStatus::FULL;
	}

	return BatteryStatus::CRITICAL;
}

int8_t PowerManager::getBatteryChargePercentage() const {
	if (battery_mv_ >= voltage_full_) {
		return 100;
	}
	if (battery_mv_ <= voltage_critical_) {
		return 0;
	}

	// Non-linear mapping that better represents Li-ion discharge curve
	// Gives more resolution in the middle range where voltage drops fastest
	const float normalized =
	    static_cast<float>(battery_mv_ - voltage_critical_) / static_cast<float>(voltage_full_ - voltage_critical_);
	const int32_t percentage = static_cast<int32_t>(normalized * normalized * 100.0F);
	return static_cast<int8_t>(std::clamp(percentage, int32_t{0}, int32_t{100}));
}

void PowerManager::displayPowerStatus() {
	auto& power_manager_instance = deluge::system::power_manager;

	// Get fresh reading
	power_manager_instance.update();

	// Format the battery status text
	char text[32];
	if (power_manager_instance.isExternalPowerConnected()) {
		strcpy(text, "USB (");
		intToString(power_manager_instance.getStableVoltage(), text + strlen(text));
		strcat(text, "mV)");
	}
	else {
		intToString(power_manager_instance.getBatteryChargePercentage(), text);
		strcat(text, "% (");
		intToString(power_manager_instance.getStableVoltage(), text + strlen(text));
		strcat(text, "mV)");
	}

	display->displayPopup(text);
}

void PowerManager::batteryLedBlink() {
	setOutputState(BATTERY_LED.port, BATTERY_LED.pin, battery_led_state_);
	int32_t blink_period = (battery_mv_ - 2630) * 3;
	blink_period = std::min(blink_period, 500_i32);
	blink_period = std::max(blink_period, 60_i32);
	uiTimerManager.setTimer(TimerName::BATT_LED_BLINK, blink_period);
	battery_led_state_ = !battery_led_state_;
}

} // namespace deluge::system
