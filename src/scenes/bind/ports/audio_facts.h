#pragma once

/// @file
/// @brief What a scene reads of the audio hardware and engine that no record holds.

#include "plugin-api/types/sync.h"

#include <cstdint>

namespace deluge::scenes::bind {

/// @brief The audio side's live facts, answered by the host.
class AudioFacts {
public:
	virtual ~AudioFacts() = default;
	/// @brief Whether a mic or line input is plugged in. It changes under the song, so no record holds it.
	[[nodiscard]] virtual bool audio_inputs_present() const = 0;
	/// @brief The voices sounding now, synth and audio clip: zero for a host that renders nothing.
	[[nodiscard]] virtual int32_t live_voice_count() const = 0;
	/// @brief Whether the line input, not the mic, is the plugged-in input. It changes under the song, so no record
	/// holds it.
	[[nodiscard]] virtual bool line_in_plugged() const = 0;
	/// @brief The song's threshold-recording mode, as the index of the menu's choices (0 is off). The song holds it
	/// only at run time, with no saved record, so the host owns it.
	[[nodiscard]] virtual int32_t song_threshold_recording() const = 0;
	/// @brief The sync type of the reverb's sidechain. The engine holds it only at run time, with no saved record, so
	/// the host owns it.
	[[nodiscard]] virtual plugin::SyncType reverb_sidechain_sync_type() const = 0;
	/// @brief Whether the audio routine is running now, when a flash write must wait.
	[[nodiscard]] virtual bool audio_busy() const = 0;
};

} // namespace deluge::scenes::bind
