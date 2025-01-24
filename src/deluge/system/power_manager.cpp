#include "power_manager.h"
#include "definitions.h"
#include "definitions_cxx.hpp"
#include "RZA1/system/iodefine.h"
#include "io/debug/log.h"

namespace deluge::system {

// Global instance
PowerManager powerManager;

PowerManager::PowerManager()
    : voltageReadingLastTime(0)
    , lastStableVoltage(0)
    , currentPowerSource(PowerSource::UNKNOWN)
    , isChargingState(false) {

    // Initialize ADC for battery voltage reading
    ADC.ADCSR = (1 << 15) |     // Start conversion
                (1 << 13) |     // Enable ADC
                (0b011 << 6) |  // Clock select
                SYS_VOLT_SENSE_PIN;  // Channel select
}

void PowerManager::update() {
    // Check if ADC conversion is complete
    if (ADC.ADCSR & (1 << 15)) {
        // Get raw 12-bit ADC reading (0-4095)
        int32_t numericReading = ADC.ADDRF & 0xFFF;

        // Convert to mV:
        // 1. Scale to 0-3300mV range (3.3V reference)
        // 2. Double the result (voltage divider compensation)
        // To avoid overflow: reading * (3300 * 2) / 4096
        int32_t voltageReading = (numericReading * 6600) / 4096;

        // Apply low-pass filter
        int32_t distanceToGo = voltageReading - lastStableVoltage;
        lastStableVoltage += distanceToGo >> 4;  // Shift by 4 for smooth filtering

        D_PRINTLN("Power: ADC=%d mV=%d", numericReading, lastStableVoltage);

        // Update power source based on voltage
        updatePowerSource();
    }

    // Always start a new conversion - this ensures we keep getting updates
    ADC.ADCSR = (1 << 15) |     // Start conversion
                (1 << 13) |     // Enable ADC
                (0b011 << 6) |  // Clock select
                SYS_VOLT_SENSE_PIN;  // Channel select
}

void PowerManager::updatePowerSource() {
    PowerSource newSource;
    bool newCharging = false;

    // Use voltage thresholds to determine power source
    if (lastStableVoltage > 4600) {  // Above 4.6V - definitely USB/DC power
        newSource = PowerSource::USB;
        newCharging = true;
    }
    else if (lastStableVoltage > 4200) {  // Above 4.2V - fully charged
        newSource = PowerSource::BATTERY;
        newCharging = false;
    }
    else if (lastStableVoltage > 2900) {  // Above 2.9V - normal battery operation
        newSource = PowerSource::BATTERY;
        newCharging = false;
    }
    else {  // Below 2.9V - critical battery
        newSource = PowerSource::BATTERY;
        newCharging = false;
    }

    // Only log if state changed
    if (newSource != currentPowerSource || newCharging != isChargingState) {
        D_PRINTLN("Power state changed: source=%d->%d charging=%d->%d voltage=%d",
                 (int)currentPowerSource, (int)newSource, isChargingState, newCharging, lastStableVoltage);
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
    if (lastStableVoltage <= BATTERY_EMPTY_MV) return 0;
    if (lastStableVoltage >= BATTERY_FULL_MV) return 100;

    return ((lastStableVoltage - BATTERY_EMPTY_MV) * 100)
           / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
}

} // namespace deluge::system
