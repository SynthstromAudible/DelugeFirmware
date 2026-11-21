#pragma once

/// @file
/// @brief What a scene asks of the device itself: saving its settings and forgetting the undo history.

namespace deluge::scenes::bind {

/// @brief The device's own actions, which the host performs as the legacy items do.
class DeviceActions {
public:
	virtual ~DeviceActions() = default;
	/// @brief Write the device's settings to its flash now: the settings file alone, not the MIDI devices or the
	///        features, which a settings menu's close saves with it.
	virtual void save_settings() = 0;
	/// @brief Forget the undo and redo history.
	virtual void delete_all_logs() = 0;
};

} // namespace deluge::scenes::bind
