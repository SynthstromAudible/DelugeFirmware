#pragma once

/// @file
/// @brief What a scene asks of an input take: a recording of the audio inputs into a new file, which the host keeps.

#include <cstdint>

namespace deluge::scenes::bind {

/// @brief How many channels a take records.
enum class TakeChannels : uint8_t {
	MONO = 1,   ///< the left input
	STEREO = 2, ///< both inputs
};

/// @brief The input's level, each channel 0 to 1, as the threshold meter shows it.
struct InputLevels {
	float left{0.F};  ///< the left channel
	float right{0.F}; ///< the right channel
	bool operator==(const InputLevels&) const = default;
};

/// @brief An input take's lifecycle, each the legacy step it names. The host holds the take between `begin` and
///        `finish` or `orphan`; a call with no take held does nothing.
class TakeActions {
public:
	virtual ~TakeActions() = default;
	/// @brief Begin a take into the RECORD folder and publish it as the foreground recording, so input monitoring
	///        follows it (`origin/main:src/deluge/gui/ui/audio_recorder.cpp`, `AudioRecorder::setupRecordingToFile`).
	///        Called with no take held; the recorder's own `armed_` guarantees it.
	/// @param channels How many channels the take records.
	/// @return False when no take could be made (no memory), or a take is already held.
	[[nodiscard]] virtual bool begin(TakeChannels channels) = 0;
	/// @brief End the take soon, with the internal button-press latency.
	virtual void end_soon() = 0;
	/// @brief Take the input's level since the last call, which the take's meter follows.
	/// @return The levels; zero with no take held.
	[[nodiscard]] virtual InputLevels take_levels() = 0;
	/// @brief Whether the take clipped since the last call; the flag clears.
	/// @return True once per clip; false with no take held.
	[[nodiscard]] virtual bool take_clipped() = 0;
	/// @brief Unpublish the settled take, then free it.
	virtual void finish() = 0;
	/// @brief Hand a running take to the foreground recording, which ends it and keeps its file attached to nothing.
	virtual void orphan() = 0;
};

} // namespace deluge::scenes::bind
