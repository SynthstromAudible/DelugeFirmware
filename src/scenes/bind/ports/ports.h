#pragma once

/// @file
/// @brief The scenes' whole host interface: one pointer per port. The host builds one set; a spec builds its own
///        from fakes and leaves null whatever its scene does not use.

#include "scenes/bind/ports/audio_facts.h"
#include "scenes/bind/ports/card_facts.h"
#include "scenes/bind/ports/sound_facts.h"
#include "scenes/bind/ports/ui_facts.h"

namespace deluge::scenes::bind {

/// @brief Non-owning pointers to the ports a scene reads and writes through. A null pointer is a port the
///        holder's host does not provide; a scene that needs one checks it before use, as it would any optional
///        dependency.
struct Ports {
	const AudioFacts* audio{nullptr}; ///< the audio hardware's and engine's live facts
	const CardFacts* card{nullptr};   ///< the loads the card is running
	const UiFacts* ui{nullptr};       ///< the UI's modes, popups and display settings
	const SoundFacts* sound{nullptr}; ///< what a sound's record does not hold: its loads, state, cables and ranges
	bool operator==(const Ports&) const = default;
};

} // namespace deluge::scenes::bind
