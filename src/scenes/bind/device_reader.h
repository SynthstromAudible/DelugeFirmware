#pragma once

/// @file
/// @brief The device's own settings, read by DEVICE row: for the bind dispatch, and as typed values for a scene that
///        branches on one.

#include "plugin-api/types/setting.h"
#include "scenes/bind/ports/device_settings.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

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

/// @name Typed reads of the DEVICE rows a scene branches on.
///
/// Every read answers what @p port holds, or the row's default where there is no port (a context that binds none).
/// @p path must be a DEVICE row's path, a `project::device_path` constant: a path no row has is a programming error,
/// which the debug build asserts on.
/// @{

/// @brief The value of the DEVICE row at @p path, in the row's stored units.
/// @param port The host's device settings, or null.
/// @param path A DEVICE row's path.
/// @return The stored value; the row's default with no port.
[[nodiscard]] int32_t read_device_value(const DeviceSettings* port, std::string_view path);

/// @brief The DEVICE CHOICE row at @p path as the enum whose enumerators are its choices' indices.
/// @tparam E A scoped enum whose values are the row's choice indices.
/// @param port The host's device settings, or null.
/// @param path A DEVICE row's path.
/// @return The choice; the row's default with no port.
template <typename E>
requires std::is_scoped_enum_v<E>
[[nodiscard]] E read_device(const DeviceSettings* port, std::string_view path) {
	return static_cast<E>(read_device_value(port, path));
}

/// @brief Whether the DEVICE BOOL row at @p path is on.
/// @param port The host's device settings, or null.
/// @param path A DEVICE row's path.
/// @return The row's value; the row's default with no port.
[[nodiscard]] bool read_device_flag(const DeviceSettings* port, std::string_view path);

/// @}

} // namespace deluge::scenes::bind
