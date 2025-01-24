#include "power_manager.h"
#include "definitions.h"
#include "hid/usb_check.h"
#include "util/functions.h"
#include "RZA1/mtu/mtu.h"
#include "RZA1/system/iodefine.h"
#include "io/debug/log.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace deluge::system {

// Global instance
PowerManager powerManager;

PowerManager::PowerManager()
    : lastStableVoltage(0)
    , currentPowerSource(PowerSource::UNKNOWN)
    , isChargingState(false)
    , lastPowerSourceChangeTime(0) {
}

void PowerManager::update() {
    // Get new voltage reading from ADC - using existing ADC pattern
    if (ADC.ADCSR & (1 << 15)) {
        // ADC is 12-bit, reading is in range 0-4095
        int32_t numericReading = ADC.ADDRF;
        // Convert to mV - scale by 3.3V reference
        int32_t voltageReading = numericReading * 3300;
        int32_t mV = voltageReading >> 12;  // Convert 12-bit reading to mV

        // Add to circular buffer
        if (!voltageSamples.full()) {
            voltageSamples.push(mV);
        }

        // Reject outliers and update power source
        rejectOutliers();
        updatePowerSource();

        // Set up for next analog read
        ADC.ADCSR = (1 << 13) | (0b011 << 6) | SYS_VOLT_SENSE_PIN;
    }
}

int32_t PowerManager::calculateSmoothedVoltage() const {
    if (voltageSamples.empty()) {
        return lastStableVoltage;
    }

    // Calculate exponential moving average
    int32_t sum = 0;
    int32_t weight = 1;
    int32_t totalWeight = 0;

    for (size_t i = 0; i < voltageSamples.size(); i++) {
        sum += voltageSamples[i] * weight;
        totalWeight += weight;
        weight = (weight * 3) >> 2;  // Multiply by 0.75
    }

    return sum / totalWeight;
}

void PowerManager::rejectOutliers() {
    if (voltageSamples.size() < 3) return;

    // Calculate mean and standard deviation
    int32_t sum = 0;
    for (size_t i = 0; i < voltageSamples.size(); i++) {
        sum += voltageSamples[i];
    }
    int32_t mean = sum / voltageSamples.size();

    int32_t sumSquaredDiff = 0;
    for (size_t i = 0; i < voltageSamples.size(); i++) {
        int32_t diff = voltageSamples[i] - mean;
        sumSquaredDiff += diff * diff;
    }
    int32_t stdDev = static_cast<int32_t>(std::sqrt(sumSquaredDiff / voltageSamples.size()));

    // Remove values more than 2 standard deviations from mean
    etl::circular_buffer<int32_t, SAMPLE_BUFFER_SIZE> newSamples;
    for (size_t i = 0; i < voltageSamples.size(); i++) {
        if (std::abs(voltageSamples[i] - mean) <= stdDev * 2) {
            newSamples.push(voltageSamples[i]);
        }
    }
    voltageSamples = newSamples;
}

void PowerManager::updatePowerSource() {
    PowerSource newSource = PowerSource::BATTERY;
    bool newChargingState = false;

    // Check USB power first
    if (detectUSBPower()) {
        newSource = PowerSource::USB;
        newChargingState = true;
    }
    // Then check DC power
    else if (detectDCPower()) {
        newSource = PowerSource::DC_POWER;
        newChargingState = true;
    }

    // Apply hysteresis to power source changes
    uint32_t currentTime = *TCNT[TIMER_SYSTEM_SLOW];
    if (newSource != currentPowerSource) {
        if ((uint32_t)(currentTime - lastPowerSourceChangeTime) > msToSlowTimerCount(POWER_SOURCE_DEBOUNCE_MS)) {
            currentPowerSource = newSource;
            isChargingState = newChargingState;
            lastPowerSourceChangeTime = currentTime;
        }
    }

    // Update stable voltage with hysteresis
    int32_t smoothedVoltage = calculateSmoothedVoltage();
    if (std::abs(smoothedVoltage - lastStableVoltage) > VOLTAGE_HYSTERESIS_MV) {
        lastStableVoltage = smoothedVoltage;
    }
}

bool PowerManager::detectUSBPower() const {
    return check_usb_power_status() != 0;
}

bool PowerManager::detectDCPower() const {
    // DC power typically shows up as a higher voltage
    return lastStableVoltage > DC_POWER_THRESHOLD_MV;
}

int32_t PowerManager::getStableVoltage() const {
    return lastStableVoltage;
}

PowerSource PowerManager::getPowerSource() const {
    return currentPowerSource;
}

bool PowerManager::isExternalPowerConnected() const {
    return currentPowerSource != PowerSource::BATTERY;
}

bool PowerManager::isCharging() const {
    return isChargingState;
}

int32_t PowerManager::getBatteryPercentage() const {
    if (lastStableVoltage <= BATTERY_EMPTY_MV) {
        return 0;
    }
    if (lastStableVoltage >= BATTERY_FULL_MV) {
        return 100;
    }

    return ((lastStableVoltage - BATTERY_EMPTY_MV) * 100) / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
}

} // namespace deluge::system
