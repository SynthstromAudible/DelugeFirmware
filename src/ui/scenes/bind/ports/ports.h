#pragma once

/// @file
/// @brief The scenes' whole host interface: one pointer per port. The host builds one set; a spec builds its own
///        from fakes and leaves null whatever its scene does not use.

#include "ui/controls/facts.h"
#include "ui/controls/sink.h"
#include "ui/scenes/bind/ports/audio_facts.h"
#include "ui/scenes/bind/ports/card_facts.h"
#include "ui/scenes/bind/ports/catalogue.h"
#include "ui/scenes/bind/ports/clip_facts.h"
#include "ui/scenes/bind/ports/clip_writes.h"
#include "ui/scenes/bind/ports/device_actions.h"
#include "ui/scenes/bind/ports/device_settings.h"
#include "ui/scenes/bind/ports/indicators.h"
#include "ui/scenes/bind/ports/learn_sink.h"
#include "ui/scenes/bind/ports/midi_devices.h"
#include "ui/scenes/bind/ports/midi_feedback.h"
#include "ui/scenes/bind/ports/sound_facts.h"
#include "ui/scenes/bind/ports/sound_writes.h"
#include "ui/scenes/bind/ports/take_actions.h"
#include "ui/scenes/bind/ports/track_facts.h"
#include "ui/scenes/bind/ports/transport.h"
#include "ui/scenes/bind/ports/transport_actions.h"
#include "ui/scenes/bind/ports/ui_facts.h"
#include "ui/scenes/bind/ports/view_actions.h"
#include "ui/scenes/bind/ports/waveforms.h"
#include "ui/scenes/bind/ports/write_sink.h"

namespace deluge::ui::scenes::bind {

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
	const ui::controls::Facts* controls{nullptr}; ///< what the knobs read: live param values, stutter, note tails
	ClipWrites* clip_writes{nullptr};             ///< where a scene's clip edits go
	SoundWrites* sound_writes{nullptr};           ///< where a scene's edits of a sound's sources go
	LearnSink* learn{nullptr};                    ///< where a scene's MIDI learns go
	MidiDevices* midi{nullptr};                   ///< the connected MIDI devices and their own settings
	WriteSink* writes{nullptr};                   ///< where a menu's setting, text and automation writes go
	ui::controls::Sink* controls_sink{nullptr};   ///< where the knobs' writes and an effect setting's exact word go
	DeviceSettings* device{nullptr};              ///< the device's own settings, by DEVICE row
	DeviceActions* device_actions{nullptr};       ///< the device's own actions: saving its settings, forgetting undo
	MidiFeedback* midi_feedback{nullptr};         ///< MIDI follow's feedback to a controller
	const Transport* transport{nullptr};          ///< the transport's state
	TransportActions* transport_actions{nullptr}; ///< what the transport is asked to do
	const Waveforms* waveforms{nullptr};          ///< the waveforms the scenes draw, by address
	TakeActions* take_actions{nullptr};           ///< an input take's lifecycle
	Indicators* indicators{nullptr};              ///< the indicators a scene changes outside its frames
	ViewActions* view_actions{nullptr};           ///< the root view's actions
	Catalogue* catalogue{nullptr};                ///< the card's file listing the browsers show
	/// @brief Whether every port the bind dispatch reads (audio, sound, tracks and controls) is present.
	/// @return True when all four are non-null.
	[[nodiscard]] bool has_bind_ports() const {
		return audio != nullptr && sound != nullptr && tracks != nullptr && controls != nullptr;
	}
	bool operator==(const Ports&) const = default;
};

/// @brief One port of an installed set, through a set that may not be installed.
/// @param ports  The set, or null when none is installed.
/// @param member The slot, as `&Ports::audio`.
/// @return The port, or null when no set is installed or the set has none.
template <typename Port>
[[nodiscard]] constexpr Port* port_of(const Ports* ports, Port* Ports::* member) {
	return ports != nullptr ? ports->*member : nullptr;
}

} // namespace deluge::ui::scenes::bind
