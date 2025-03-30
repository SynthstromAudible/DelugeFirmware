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

#pragma once

#include <cstdint>

namespace deluge::system {

enum class PowerSource { BATTERY, USB, DC_POWER, UNKNOWN };
enum class BatteryStatus {
	CRITICAL, // < 2900mV
	WARNING,  // 2901-3300mV
	HEALTHY,  // 3301-4200mV
	FULL      // > 4200mV
};

class PowerManager {
public:
	PowerManager();
	~PowerManager() = default;

	void update();
	void displayPowerStatus();
	int32_t getStableVoltage() const;
	PowerSource getPowerSource() const;
	bool isExternalPowerConnected() const;
	int8_t getBatteryChargePercentage() const;
	bool isCharging() const;
	void batteryLEDBlink();
	BatteryStatus getBatteryStatus() const;
	int32_t getVoltageReading() const;
	int8_t batteryChargePercentage() const;

private:
	static constexpr int32_t VOLTAGE_BUFFER_SIZE = 32;
	int32_t voltageReadings[VOLTAGE_BUFFER_SIZE];
	int32_t voltageBufferIndex = 0;
	int32_t numReadings = 0;

	// Voltage thresholds (these really should be calibrated somehow)
	static constexpr int32_t VOLTAGE_CHARGING = 4900;
	static constexpr int32_t VOLTAGE_FULL = 4200;
	static constexpr int32_t VOLTAGE_NOMINAL = 3700;
	static constexpr int32_t VOLTAGE_LOW = 3300;
	static constexpr int32_t VOLTAGE_CRITICAL = 2900;

	mutable int32_t voltageReadingLastTime = 65535 * 3300;
	uint16_t batteryMV;
	bool batteryLEDState = false;

	PowerSource currentPowerSource;
};
// Global instance
extern PowerManager powerManager;

} // namespace deluge::system
