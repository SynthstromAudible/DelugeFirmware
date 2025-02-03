#pragma once

#include <cstdint>

namespace deluge::system {

enum class PowerSource { BATTERY, USB, DC_POWER, UNKNOWN };

class PowerManager {
public:
		PowerManager();
		~PowerManager() = default;

		void update();
		int32_t getStableVoltage() const;
		PowerSource getPowerSource() const;
		bool isExternalPowerConnected() const;
		int32_t getBatteryPercentage() const;
		bool isCharging() const;

		// Battery voltage thresholds in mV
		static constexpr int32_t USB_THRESHOLD_MV = 4500;
		static constexpr int32_t BATTERY_FULL_MV = 4200;
		static constexpr int32_t BATTERY_EMPTY_MV = 3000;

private:
		int32_t voltageReadingLastTime; // For voltage filtering
		int32_t lastStableVoltage;      // Current stable voltage in mV
		PowerSource currentPowerSource;
		bool isChargingState;

		void updatePowerSource();
};
// Global instance
extern PowerManager powerManager;

} // namespace deluge::system
