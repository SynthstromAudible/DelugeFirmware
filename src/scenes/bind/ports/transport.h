#pragma once

/// @file
/// @brief What a scene reads of the transport: playback, the clocks, recording, the metronome, the count-in and what
///        undo allows.

#include <cstdint>

namespace deluge::scenes::bind {

/// @brief What the transport records into.
enum class TransportRecording : uint8_t {
	OFF = 0,         ///< not recording
	NORMAL = 1,      ///< recording into the session's clips
	ARRANGEMENT = 2, ///< recording to the arrangement
};

/// @brief The transport's live state, answered by the host.
class Transport {
public:
	virtual ~Transport() = default;
	/// @brief Whether playback is on: any of the playback state's bits is set.
	[[nodiscard]] virtual bool playing() const = 0;
	/// @brief Whether the internal or an external clock is driving playback.
	[[nodiscard]] virtual bool clocks_active() const = 0;
	/// @brief What the transport records into.
	[[nodiscard]] virtual TransportRecording recording() const = 0;
	/// @brief Whether the metronome is on.
	[[nodiscard]] virtual bool metronome_on() const = 0;
	/// @brief Whether an undo or redo may run now: a song is loaded, the current UI is the root view, and the UI mode
	///        is one a reversion allows.
	[[nodiscard]] virtual bool undo_allowed() const = 0;
	/// @brief Whether any session clip waits to overdub at its next launch, which a song save refuses; false with no
	/// song.
	[[nodiscard]] virtual bool overdubs_pending() const = 0;
	/// @brief The count-in's digit while a recording counts in.
	/// @return The digit; 0 when no count-in shows.
	[[nodiscard]] virtual int32_t count_in_visual() const = 0;
};

} // namespace deluge::scenes::bind
