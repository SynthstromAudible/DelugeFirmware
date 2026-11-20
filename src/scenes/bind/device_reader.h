#pragma once

/// @file
/// @brief The device's own settings, read by DEVICE row for the bind dispatch.

#include "plugin-api/types/setting.h"
#include "scenes/bind/ports/device_settings.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief Reads the DEVICE rows through the host's device port: each row's stored value, whether or not it applies.
/// The device is always there, so the reader always resolves; it reads no song.
class DeviceReader {
public:
	/// @param device The host's device settings; must outlive this reader.
	explicit DeviceReader(const DeviceSettings& device) : device_{&device} {}

	/// @brief Whether the owner resolves: the device always does.
	[[nodiscard]] bool resolved() const { return true; }
	/// @brief The stored value of the DEVICE row at @p path.
	/// @param path A DEVICE row's path.
	/// @return The value, or nullopt for a path no DEVICE row has.
	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const;
	/// @brief This reader as the engine's SettingReader; valid while the device port lives.
	[[nodiscard]] plugin::SettingReader reader() const;

private:
	const DeviceSettings* device_;
};

} // namespace deluge::scenes::bind
