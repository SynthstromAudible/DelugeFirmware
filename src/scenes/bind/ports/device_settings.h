#pragma once

/// @file
/// @brief The device's own settings, by DEVICE row: what a scene reads and writes of them.

#include "plugin-api/types/setting.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief What a write of a device setting did.
enum class DeviceWrite : uint8_t {
	REFUSED,            ///< no such row, or the value is outside it: nothing changed
	STORED,             ///< stored, and in effect now
	STORED_FOR_RESTART, ///< stored; it takes effect when the device restarts
};

/// @brief The DEVICE setting group's values, which the host keeps where the device keeps them.
///
/// The public members check every path and value against the group's rows, so a host or a fake implements only the
/// storage and never sees a path or a value the rows refuse.
class DeviceSettings {
public:
	virtual ~DeviceSettings() = default;

	/// @brief The stored value of the DEVICE row at @p path, in the row's stored units.
	///
	/// A CHOICE row whose held value is an index past its choices reads clamped into `[0, choices - 1]`, as a choice
	/// item's widget shows it; any other value the row cannot name (a word it has no choice for, unreadable text)
	/// reads as the row's default. Reading changes nothing the device holds.
	/// @param path A DEVICE row's path.
	/// @return The value, or nullopt when no DEVICE row has that path.
	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const;

	/// @brief A reader over the DEVICE rows, for the plugin-api's setting helpers. Valid while this port lives.
	[[nodiscard]] plugin::SettingReader reader() const;

	/// @brief Stores @p menu_value, in the row's menu units, in the DEVICE row at @p path.
	/// @param path A DEVICE row's path.
	/// @param menu_value The value as the menu shows it.
	/// @return What the write did. Refused leaves every setting as it was.
	[[nodiscard]] DeviceWrite write(std::string_view path, int32_t menu_value);

private:
	/// @brief The file-form value the device holds for @p row, which is a DEVICE row: a CHOICE row's word, or for an
	///        index the row has no choice at, that index as a number (which `read` clamps).
	[[nodiscard]] virtual serialization::Value load(const plugin::SettingInfo& row) const = 0;

	/// @brief Stores @p value, already checked against @p row, and applies what storing it implies.
	/// @return STORED or STORED_FOR_RESTART; REFUSED only when the implementation keeps @p row nowhere, a row it has no
	///         home for, with nothing changed. The value checks are done by the time it is called.
	[[nodiscard]] virtual DeviceWrite store(const plugin::SettingInfo& row, const serialization::Value& value) = 0;
};

} // namespace deluge::scenes::bind
