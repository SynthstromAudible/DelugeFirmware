#include "power_manager.h"
#include "RZA1/system/iodefine.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "io/debug/log.h"

namespace deluge::system {

// Global instance
PowerManager powerManager;

PowerManager::PowerManager()
    : voltageReadingLastTime(0), lastStableVoltage(0), currentPowerSource(PowerSource::UNKNOWN),
      isChargingState(false) {

	// Initialize ADC for battery voltage reading
	ADC.ADCSR = (1 << 15) |         // Start conversion
	            (1 << 13) |         // Enable ADC
	            (0b011 << 6) |      // Clock select
	            SYS_VOLT_SENSE_PIN; // Channel select
}

void PowerManager::update() {
	// Check if ADC conversion is complete
	if (ADC.ADCSR & (1 << 15)) {
		// Match original calculation exactly
		int32_t numericReading = ADC.ADDRF;
		int32_t voltageReading = numericReading * 3300;
		int32_t distanceToGo = voltageReading - voltageReadingLastTime;
		voltageReadingLastTime += distanceToGo >> 4;

		// Match original right shift by 15
		lastStableVoltage = voltageReadingLastTime >> 15;

		updatePowerSource();
	}

	// Start next conversion
	ADC.ADCSR = (1 << 15) |         // Start conversion
	            (1 << 13) |         // Enable ADC
	            (0b011 << 6) |      // Clock select
	            SYS_VOLT_SENSE_PIN; // Channel select
}

void PowerManager::updatePowerSource() {
	PowerSource newSource;
	bool newCharging = false;

	// Use voltage thresholds to determine power source
	if (lastStableVoltage > 4600) { // Above 4.6V - definitely USB/DC power
		newSource = PowerSource::USB;
		newCharging = true;
	}
	else if (lastStableVoltage > 4200) { // Above 4.2V - fully charged
		newSource = PowerSource::BATTERY;
		newCharging = false;
	}
	else if (lastStableVoltage > 2900) { // Above 2.9V - normal battery operation
		newSource = PowerSource::BATTERY;
		newCharging = false;
	}
	else { // Below 2.9V - critical battery
		newSource = PowerSource::BATTERY;
		newCharging = false;
	}

	// Only log if state changed
	if (newSource != currentPowerSource || newCharging != isChargingState) {
		D_PRINTLN("Power state changed: source=%d->%d charging=%d->%d voltage=%d", (int)currentPowerSource,
		          (int)newSource, isChargingState, newCharging, lastStableVoltage);
	}

	currentPowerSource = newSource;
	isChargingState = newCharging;
}

int32_t PowerManager::getStableVoltage() const {
	return lastStableVoltage;
}

PowerSource PowerManager::getPowerSource() const {
	return currentPowerSource;
}

bool PowerManager::isExternalPowerConnected() const {
	return currentPowerSource == PowerSource::USB;
}

bool PowerManager::isCharging() const {
	return isChargingState;
}

int32_t PowerManager::getBatteryPercentage() const {
	// Critical battery - below 2900mV
	if (lastStableVoltage < 2900)
		return 0;

	// Low battery range: 2900mV - 2950mV (0-25%)
	if (lastStableVoltage < 2950) {
		return ((lastStableVoltage - 2900) * 25) / 50;
	}

	// Medium battery range: 2950mV - 3300mV (25-90%)
	if (lastStableVoltage < 3300) {
		return 25 + ((lastStableVoltage - 2950) * 65) / 350;
	}

	// High battery range: 3300mV+ (90-100%)
	if (lastStableVoltage < 3600) {
		return 90 + ((lastStableVoltage - 3300) * 10) / 300;
	}

	return 100;
}

} // namespace deluge::system
