#pragma once

/// @file
/// @brief Where a scene's MIDI learns go: what a mapping target is played by, which knob drives a param, and what a
///        global command or a follow input is learned to.

#include "scenes/bind/ports/midi_devices.h"

#include "deluge-project/device_settings.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "deluge-project/param_address.h"
#include "midi/device_id.h"

#include <cstdint>
#include <optional>

namespace deluge::scenes::bind {

/// @brief A note heard on a track's or kit row's input learn: the device it came on and its channel or MPE zone,
///        which the learn takes the bend ranges and velocity default of.
struct LearnInput {
	midi::DeviceId device{};    ///< the device the note came on
	int32_t channel_or_zone{0}; ///< its channel, or its MPE zone
	bool operator==(const LearnInput&) const = default;
};

/// @brief Takes a scene's learns. The app applies each; a spec records them.
class LearnSink {
public:
	virtual ~LearnSink() = default;
	/// @brief Learn @p command as what @p target is played by, or unlearn @p target when @p command is nullopt. Any
	///        unlearn marks the current instrument edited, as @ref learn_knob's learn does; the menu is the only
	///        caller that unlearns, and unlearns only a param's knob.
	/// @param target  What the command plays.
	/// @param command The command learned, or nullopt to unlearn.
	/// @param input   A note learned on a track's or kit row's input: what the learn takes from its device.
	/// @return Whether it was taken: false where @p target's owner is not in the song.
	virtual bool learn(project::MappingTarget target, std::optional<project::MidiCommand> command,
	                   std::optional<LearnInput> input) = 0;
	/// @brief Learn a knob onto @p target's param: a mod knob when @p midi_channel is 16 or more, in the current
	///        output's knob mode; otherwise a MIDI knob heard on @p device. A learned knob marks the current
	///        instrument edited.
	/// @param target       The owner and the param.
	/// @param device       The device a MIDI knob was heard on; none for a mod knob.
	/// @param which_knob   The mod knob (0 lower, 1 upper), or the CC number (128 for pitch bend).
	/// @param midi_channel The channel a MIDI knob was heard on, or 16 or more for a mod knob.
	/// @return Whether the knob was learned.
	virtual bool learn_knob(const project::OwnerParamTarget& target, midi::DeviceId device, int32_t which_knob,
	                        int32_t midi_channel) = 0;
	/// @brief Learn @p binding as what @p command is taken from, or unlearn @p command when @p binding is nullopt. A
	///        binding naming a device `MidiDevices` does not know is refused, and nothing is stored. Nothing is
	///        saved here: the settings menu saves when it closes.
	/// @param command The global command.
	/// @param binding The binding learned, or nullopt to unlearn.
	virtual void learn_global(project::GlobalCommand command, std::optional<LearnedBinding> binding) = 0;
	/// @brief Learn @p binding as what @p input listens on, or unlearn @p input when @p binding is nullopt. A binding
	///        naming a device `MidiDevices` does not know is refused, and nothing is stored. Nothing is saved here.
	/// @param input   The follow input.
	/// @param binding The binding learned, or nullopt to unlearn.
	virtual void learn_follow(project::FollowInput input, std::optional<LearnedBinding> binding) = 0;
};

} // namespace deluge::scenes::bind
