#pragma once

/// @file
/// @brief What a scene reads of the audio hardware and engine that no record holds.

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
};

} // namespace deluge::scenes::bind
