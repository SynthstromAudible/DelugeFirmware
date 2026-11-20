#pragma once

/// @file
/// @brief The device's own settings, read by DEVICE row: for the bind dispatch, and as typed values for a scene that
///        branches on one.

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
	/// @brief A reader over @p device's rows.
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

/// @brief The value of the DEVICE row at @p path: what @p port holds, or the row's default when there is no port.
/// @param port The host's device settings, or null where a context binds none.
/// @param path A DEVICE row's path (a `project::device_path` constant).
/// @return The row's stored value, or nullopt when no DEVICE row has that path.
[[nodiscard]] std::optional<int32_t> read_device_value(const DeviceSettings* port, std::string_view path);

/// @brief The DEVICE CHOICE row at @p path as the enum whose enumerators are its choices' indices.
/// @tparam E An `enum class` whose values are the row's choice indices.
/// @param port The host's device settings, or null where a context binds none: the row's default then.
/// @param path A DEVICE row's path (a `project::device_path` constant).
/// @return The choice, or nullopt when no DEVICE row has that path.
template <typename E>
[[nodiscard]] std::optional<E> read_device(const DeviceSettings* port, std::string_view path) {
	const std::optional<int32_t> value = read_device_value(port, path);
	if (!value.has_value()) {
		return std::nullopt;
	}
	return static_cast<E>(*value);
}

/// @brief Whether the DEVICE BOOL row at @p path is on.
/// @param port The host's device settings, or null where a context binds none.
/// @param path A DEVICE row's path (a `project::device_path` constant).
/// @return The row's value; false with no port or no such row.
[[nodiscard]] bool read_device_flag(const DeviceSettings* port, std::string_view path);

} // namespace deluge::scenes::bind
