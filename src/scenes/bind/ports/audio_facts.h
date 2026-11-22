#pragma once

/// @file
/// @brief What a scene reads of the audio hardware and engine that no record holds.

#include "plugin/types/sync.h"

#include <cstdint>
#include <optional>
#include <string>

namespace deluge::scenes::bind {

/// @brief What the foreground recorder records from.
enum class ForegroundRecording : uint8_t {
	UNSET = 0,    ///< no source chosen yet
	NONE = 1,     ///< nothing
	EXTERNAL = 2, ///< an input: the mic, line in, balanced or USB audio
	INTERNAL = 3, ///< the device's own output: the mix, the output, one track's, or an offline stem
};

/// @brief An input take's state, as the recorder screen reads it on every frame.
struct InputTake {
	bool capturing{false};      ///< past the threshold's margin: Recording rather than Waiting
	bool threshold{false};      ///< a threshold take, which shows the meter
	float threshold_level{0.F}; ///< the threshold, 0 to 1
	bool capturing_data{false}; ///< still writing; an end may be asked for
	bool settled{false};        ///< closed, with no card write still out: it may be finished
	bool aborted{false};        ///< it ended with nothing to keep
	bool card_error{false};     ///< the card failed under it
	uint8_t channels{0};        ///< 1 or 2
	bool operator==(const InputTake&) const = default;
};

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
	/// @brief What the foreground recorder records from.
	[[nodiscard]] virtual ForegroundRecording foreground_recording() const = 0;
	/// @brief Whether the foreground recorder is resampling the device's output.
	[[nodiscard]] virtual bool resampling() const = 0;
	/// @brief The input take `TakeActions` holds.
	/// @return Its state; none when no take is held.
	[[nodiscard]] virtual std::optional<InputTake> input_take() const = 0;
	/// @brief The held take's file.
	/// @return Its path on the card, empty until the take's file is made; none when no take is held.
	[[nodiscard]] virtual std::optional<std::string> input_take_path() const = 0;
	/// @brief The audio clock, in frames at the device's rate.
	[[nodiscard]] virtual uint32_t audio_sample_timer() const = 0;
};

} // namespace deluge::scenes::bind
