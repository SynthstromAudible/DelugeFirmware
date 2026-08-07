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
	void batteryLedBlink();
	BatteryStatus getBatteryStatus() const;
	int32_t getVoltageReading() const;

private:
	// Voltage thresholds (these really should be calibrated somehow)
	static constexpr int32_t voltage_charging_ = 4900;
	static constexpr int32_t voltage_full_ = 4200;
	static constexpr int32_t voltage_low_ = 3300;
	static constexpr int32_t voltage_critical_ = 2900;

	mutable int32_t voltage_reading_last_time_ = 65535 * 3300;
	int32_t battery_mv_ = 0;
	bool battery_led_state_ = false;

	PowerSource current_power_source_ = PowerSource::UNKNOWN;
};
// Global instance
extern PowerManager power_manager;

} // namespace deluge::system
