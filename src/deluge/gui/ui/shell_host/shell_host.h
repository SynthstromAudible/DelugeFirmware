#pragma once

/// @file
/// @brief The app's implementations of the scenes' ports, one per file in this directory, and the one set of ports
///        built from them. Only these files name the legacy runtime objects the scenes read.

#include "scenes/bind/ports/ports.h"

class AudioClip;

namespace deluge::edits {
class Edit;
} // namespace deluge::edits

namespace deluge::gui::shell_host {

/// @brief The host's AudioFacts.
[[nodiscard]] const scenes::bind::AudioFacts& audio_facts();
/// @brief The host's CardFacts.
[[nodiscard]] const scenes::bind::CardFacts& card_facts();
/// @brief The host's UiFacts.
[[nodiscard]] const scenes::bind::UiFacts& ui_facts();

/// @brief The host's SoundFacts.
[[nodiscard]] const scenes::bind::SoundFacts& sound_facts();

/// @brief The host's ClipFacts.
[[nodiscard]] const scenes::bind::ClipFacts& clip_facts();

/// @brief The host's TrackFacts.
[[nodiscard]] const scenes::bind::TrackFacts& track_facts();

/// @brief The host's controls::Facts: the knobs' reads.
[[nodiscard]] const controls::Facts& control_facts();

/// @brief The host's ClipWrites: the clip edits, applied to the current song.
[[nodiscard]] scenes::bind::ClipWrites& clip_writes();

/// @brief The host's SoundWrites: the sound's source edits, applied to the current song.
[[nodiscard]] scenes::bind::SoundWrites& sound_writes();

/// @brief The host's LearnSink: the learns, applied to the current song.
[[nodiscard]] scenes::bind::LearnSink& learn_sink();

/// @brief Apply @p edit to the current song: the one way the write ports apply an Edit.
/// @param edit The edit. The caller has checked that a song is loaded.
/// @return Whether the edit applied (`Error::NONE`).
[[nodiscard]] bool applied(edits::Edit& edit);

/// @brief The audio clip @p clip addresses in the current song, or null: shared by the clip port and the waveforms.
[[nodiscard]] ::AudioClip* audioClipAt(project::ClipAddress clip);

} // namespace deluge::gui::shell_host
