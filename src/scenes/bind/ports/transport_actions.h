#pragma once

/// @file
/// @brief What a scene asks the transport to do: the PLAY, RECORD, TAP TEMPO and tempo-encoder actions, undo, the
///        output recording, fill and the tempo grab.

#include "project/clip_address.h"

namespace deluge::scenes::bind {

/// @brief The transport's actions, which the host performs as the legacy buttons do, popups included.
class TransportActions {
public:
	virtual ~TransportActions() = default;
	/// @brief The PLAY button: start or stop playback, with the internal button-press latency.
	virtual void play() = 0;
	/// @brief The RECORD button's short press: arm or disarm recording.
	virtual void record() = 0;
	/// @brief A TAP TEMPO press.
	/// @param normal Whether taps set the tempo the normal way; false for the alternative behaviour.
	virtual void tap_tempo(bool normal) = 0;
	/// @brief Turn the metronome on or off, with its popup.
	virtual void toggle_metronome() = 0;
	/// @brief Clear the song's tempo automation, with its popup.
	virtual void clear_tempo_automation() = 0;
	/// @brief Show the swing interval in a popup.
	virtual void show_swing_interval() = 0;
	/// @brief Show the tempo in a popup.
	virtual void show_tempo() = 0;
	/// @brief Undo, or redo, now: the reversion runs before this returns, and may redraw the views.
	/// @param redo Redo rather than undo.
	virtual void undo(bool redo) = 0;
	/// @brief Start recording the device's output to a file.
	virtual void begin_output_recording() = 0;
	/// @brief End the output recording soon, with the internal button-press latency.
	virtual void end_recording_soon() = 0;
	/// @brief End the output recording when the playing loop ends; nothing when no playback mode is running.
	virtual void stop_output_recording_at_loop_end() = 0;
	/// @brief Hold fill mode on or off; nothing with no song.
	/// @param on Fill on.
	virtual void fill(bool on) = 0;
	/// @brief Set the tempo from @p clip's sample length; nothing where it names no audio clip.
	/// @param clip The clip whose sample length gives the tempo.
	virtual void grab_tempo(project::ClipAddress clip) = 0;
};

} // namespace deluge::scenes::bind
