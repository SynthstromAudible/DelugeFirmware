#pragma once

#include <cstdint>
#include <etl/circular_buffer.h>

namespace deluge::system {

enum class PowerSource {
    BATTERY,
    USB,
    DC_POWER,
    UNKNOWN
};

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

private:
    static constexpr int SAMPLE_BUFFER_SIZE = 32;
    static constexpr int VOLTAGE_HYSTERESIS_MV = 50;
    static constexpr int POWER_SOURCE_DEBOUNCE_MS = 500;

    // Battery voltage thresholds in mV
    static constexpr int32_t BATTERY_FULL_MV = 4200;    // Full battery voltage
    static constexpr int32_t BATTERY_EMPTY_MV = 3300;   // Empty battery voltage
    static constexpr int32_t DC_POWER_THRESHOLD_MV = 4600;  // USB/DC power detection threshold

    etl::circular_buffer<int32_t, SAMPLE_BUFFER_SIZE> voltageSamples;
    int32_t lastStableVoltage;
    PowerSource currentPowerSource;
    bool isChargingState;
    uint32_t lastPowerSourceChangeTime;

    void updatePowerSource();
    int32_t calculateSmoothedVoltage() const;
    bool detectUSBPower() const;
    bool detectDCPower() const;
    void rejectOutliers();
};

// Global instance
extern PowerManager powerManager;

} // namespace deluge::system
