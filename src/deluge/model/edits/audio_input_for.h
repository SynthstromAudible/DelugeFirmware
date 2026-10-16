#pragma once

/// @file
/// @brief A gesture's change of an audio track's input, by its engine values: each goes through the audio-input
///        setting's Edit (`audioInputSettingFor`), which writes the track's record and hands the monitoring over.

#include "definitions_cxx.hpp"

class AudioOutput;
class Output;
enum class AudioOutputMode : uint8_t;

namespace deluge::edits {

/// @brief Set @p output's input channel; leaving a specific track lets go of it.
void audioInputChannelFor(::AudioOutput& output, AudioInputChannel channel);

/// @brief Set the track @p output records from, or none.
void audioInputSourceFor(::AudioOutput& output, ::Output* source);

/// @brief Set @p output's mode.
void audioModeFor(::AudioOutput& output, AudioOutputMode mode);

/// @brief Step @p output's mode by @p offset, wrapping, and redraw the OLED, which shows it on the clip screen.
void scrollAudioModeFor(::AudioOutput& output, int32_t offset);

} // namespace deluge::edits
