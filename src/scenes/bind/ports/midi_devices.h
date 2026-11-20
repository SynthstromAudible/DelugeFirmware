#pragma once

/// @file
/// @brief The MIDI devices the menu lists, their names, and the settings each keeps for itself.

#include "midi/device_id.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief A device's direction: its input port or its output port.
enum class MidiDirection : uint8_t {
	IN = 0,  ///< the port the Deluge receives on
	OUT = 1, ///< the port the Deluge sends on
};

/// @brief One port's MPE zones: the last member channel of each, 0 (lower) or 15 (upper) meaning that zone is off.
struct MpeZones {
	uint8_t lower_last_member{0};  ///< the lower zone's last member channel; 0 is off
	uint8_t upper_last_member{15}; ///< the upper zone's last member channel; 15 is off
	bool operator==(const MpeZones&) const = default;
};

/// @brief What a device keeps for itself, which its menu edits.
struct MidiDeviceSettings {
	/// @brief The velocity-to-level cable amount a sound on it takes by default, Q2.30 (the menu's top value is
	///        `1 << 30`); 0 is none.
	int32_t velocity_to_level{0};
	bool send_clock{false};          ///< clock is sent to it
	bool receive_clock{false};       ///< clock is taken from it
	bool relative{false};            ///< it sends relative CCs (MIDI takeover)
	bool thru{false};                ///< its input is sent to its output
	std::array<MpeZones, 2> zones{}; ///< each port's MPE zones, by MidiDirection
	bool operator==(const MidiDeviceSettings&) const = default;
};

/// @brief The connected MIDI devices and their own settings, answered and applied by the host.
class MidiDevices {
public:
	virtual ~MidiDevices() = default;
	/// @brief The connected devices, in the menu's order: DIN, the first two upstream USB ports, then the hosted
	///        devices in the host's order.
	/// @return The devices; valid until the next call.
	[[nodiscard]] virtual std::span<const midi::DeviceId> connected() const = 0;
	/// @brief @p device's display name.
	/// @param device The device.
	/// @return Its name; empty for none or a device the host does not know.
	[[nodiscard]] virtual std::string_view name(midi::DeviceId device) const = 0;
	/// @brief @p device's own settings.
	/// @param device The device.
	/// @return Its settings; none for a device the host does not know.
	[[nodiscard]] virtual std::optional<MidiDeviceSettings> settings(midi::DeviceId device) const = 0;
	/// @brief Make @p settings @p device's. The host applies what changed: an output's MPE zones are announced to the
	///        device, the smallest zones are recounted, and the devices are marked to be saved. Settings equal to the
	///        device's own change nothing, and the write applies nothing.
	/// @param device   The device.
	/// @param settings Its new settings, whole.
	/// @return Whether it was taken: false for a device the host does not know; true for a known one, a write that
	///         changes nothing included.
	virtual bool write(midi::DeviceId device, const MidiDeviceSettings& settings) = 0;
};

} // namespace deluge::scenes::bind
