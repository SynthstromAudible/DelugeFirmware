#pragma once

/// @file
/// @brief What a scene asks of the device itself: saving its settings, forgetting the undo history and the resets.

namespace deluge::ui::scenes::bind {

/// @brief The device's own actions, which the host performs as the legacy items do.
class DeviceActions {
public:
	virtual ~DeviceActions() = default;
	/// @brief Write the device's settings to its flash now: the settings file alone, not the MIDI devices or the
	///        features, which a settings menu's close saves with it.
	virtual void save_settings() = 0;
	/// @brief Forget the undo and redo history.
	virtual void delete_all_logs() = 0;
	/// @brief Reset the device's flash settings to the factory's, and re-apply the ones read live.
	virtual void reset_settings() = 0;
	/// @brief Reset the community features to their defaults, and re-apply the ones read live.
	virtual void reset_community_features() = 0;
	/// @brief Forget MIDI follow's learned controls and channels.
	virtual void reset_midi_follow() = 0;
	/// @brief Forget every MIDI device's own settings.
	virtual void reset_midi_devices() = 0;
	/// @brief Reset everything the device keeps, as the factory reset does, and re-apply the settings read live.
	virtual void factory_reset() = 0;
};

} // namespace deluge::ui::scenes::bind
