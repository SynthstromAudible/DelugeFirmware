#pragma once

/// @file
/// @brief The scenes' whole host interface: one pointer per port. The host builds one set; a spec builds its own
///        from fakes and leaves null whatever its scene does not use.

#include "controls/facts.h"
#include "scenes/bind/ports/audio_facts.h"
#include "scenes/bind/ports/card_facts.h"
#include "scenes/bind/ports/clip_facts.h"
#include "scenes/bind/ports/clip_writes.h"
#include "scenes/bind/ports/learn_sink.h"
#include "scenes/bind/ports/midi_devices.h"
#include "scenes/bind/ports/sound_facts.h"
#include "scenes/bind/ports/sound_writes.h"
#include "scenes/bind/ports/track_facts.h"
#include "scenes/bind/ports/ui_facts.h"

namespace deluge::scenes::bind {

/// @brief Non-owning pointers to the ports a scene reads and writes through. A null pointer is a port the
///        holder's host does not provide; a scene that needs one checks it before use, as it would any optional
///        dependency.
struct Ports {
	const AudioFacts* audio{nullptr};  ///< the audio hardware's and engine's live facts
	const CardFacts* card{nullptr};    ///< the loads the card is running
	const UiFacts* ui{nullptr};        ///< the UI's modes, popups and display settings
	const SoundFacts* sound{nullptr};  ///< what a sound's record does not hold: its loads, state, cables and ranges
	const ClipFacts* clips{nullptr};   ///< what a clip's record does not hold: its play state and view state
	const TrackFacts* tracks{nullptr}; ///< the song's tracks and what its owners play that their records do not hold
	const controls::Facts* controls{nullptr}; ///< what the knobs read: live param values, stutter, note tails
	ClipWrites* clip_writes{nullptr};         ///< where a scene's clip edits go
	SoundWrites* sound_writes{nullptr};       ///< where a scene's edits of a sound's sources go
	LearnSink* learn{nullptr};                ///< where a scene's MIDI learns go
	MidiDevices* midi{nullptr};               ///< the connected MIDI devices and their own settings
	/// @brief Whether every port the bind dispatch reads (audio, sound, tracks and controls) is present.
	/// @return True when all four are non-null.
	[[nodiscard]] bool has_bind_ports() const {
		return audio != nullptr && sound != nullptr && tracks != nullptr && controls != nullptr;
	}
	bool operator==(const Ports&) const = default;
};

} // namespace deluge::scenes::bind
