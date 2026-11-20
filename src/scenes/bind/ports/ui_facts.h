#pragma once

/// @file
/// @brief What a scene reads of the UI around it: modes, popups and display settings.

#include "deluge-project/device_settings.h"

#include <cstdint>
#include <string_view>

namespace deluge::scenes::bind {

/// @brief The performance view's editing mode.
enum class PerformanceEditing : uint8_t {
	Disabled = 0, ///< not editing: the view's pads play its FX columns
	Value = 1,    ///< editing the columns' values
	Param = 2,    ///< editing which param each column holds
};

/// @brief The UI's live facts around a scene, answered by the host.
class UiFacts {
public:
	virtual ~UiFacts() = default;
	/// @brief Whether a note is being auditioned.
	[[nodiscard]] virtual bool auditioning() const = 0;
	/// @brief Whether a popup or notification is up.
	[[nodiscard]] virtual bool popup_showing() const = 0;
	/// @brief How a playhead is shown.
	[[nodiscard]] virtual project::FlashCursor flash_cursor() const = 0;
	/// @brief The performance view's editing mode, which the performance-editing page's cursor starts on.
	[[nodiscard]] virtual PerformanceEditing performance_editing_mode() const = 0;
	/// @brief Whether the DX7 engine feature is on, which gives the DX7 parameter page its own pad layout.
	[[nodiscard]] virtual bool dx7_engine_enabled() const = 0;
	/// @brief The battery's voltage as the ADC last reported it, which the settings page's battery item shows.
	/// @return Millivolts; 0 when nothing has been read yet.
	[[nodiscard]] virtual int32_t battery_millivolts() const = 0;
	/// @brief The firmware version the settings page shows.
	/// @return A view over a string with static storage: it stays valid for the life of the program and answers the
	///         same data on every call.
	[[nodiscard]] virtual std::string_view firmware_version() const = 0;
};

} // namespace deluge::scenes::bind
