#pragma once

/// @file
/// @brief The MIDI devices the menu lists, their names, and the settings each keeps for itself.

#include "deluge-project/device_settings.h"
#include "deluge-project/midi_command.h"
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

/// @brief One learned MIDI input: a global command's or a follow input's. A global command's always names a channel; a
///        follow input's may name only a device, as a settings file can leave it.
struct LearnedBinding {
	/// The device it must come from when inputs are told apart by device; none means any device.
	std::optional<midi::DeviceId> device;
	/// A channel 0–15 or an MPE zone (16 lower, 17 upper), from `project::kFirstCcChannel` the same for a CC, and from
	/// `project::kFirstProgramChangeChannel` for a program change; none for a follow input whose channel is unassigned.
	std::optional<uint8_t> channel_or_zone;
	/// The note, CC or program number; none for an input that names only a channel (a follow input's).
	std::optional<uint8_t> note_or_cc;
	bool operator==(const LearnedBinding&) const = default;
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
	/// @brief How a song names @p device: a built-in port, or a hosted USB device by its name and USB ids.
	/// @param device The device.
	/// @return Its reference; none for a device the host does not know, or one a song cannot name.
	[[nodiscard]] virtual std::optional<project::MidiDeviceRef> reference(midi::DeviceId device) const = 0;
	/// @brief Make @p settings @p device's. The host applies what changed: an output's MPE zones are announced to the
	///        device, the smallest zones are recounted, and the devices are marked to be saved. Settings equal to the
	///        device's own change nothing, and the write applies nothing.
	/// @param device   The device.
	/// @param settings Its new settings, whole.
	/// @return Whether it was taken: false for a device the host does not know; true for a known one, a write that
	///         changes nothing included.
	virtual bool write(midi::DeviceId device, const MidiDeviceSettings& settings) = 0;
	/// @brief What @p command is learned to.
	/// @param command The global command.
	/// @return Its binding, which names a channel; none when no channel is learned.
	[[nodiscard]] virtual std::optional<LearnedBinding> global_command(project::GlobalCommand command) const = 0;
	/// @brief What @p input is learned to.
	/// @param input The follow input.
	/// @return Its binding, which may name a device and no channel; none when it names neither.
	[[nodiscard]] virtual std::optional<LearnedBinding> follow_input(project::FollowInput input) const = 0;
};

} // namespace deluge::scenes::bind
