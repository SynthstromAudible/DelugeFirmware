#include "gui/ui/shell_host/shell_host.h"

#include "definitions_cxx.hpp"
#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/device_registry.h"
#include "io/midi/learned_midi.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/project_bridge/midi_command.h"
#include "project/device_settings.h"
#include "project/midi_command.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace deluge::gui::shell_host {
namespace {

using project::FollowInput;
using project::GlobalCommand;
using scenes::bind::LearnedBinding;
using scenes::bind::MidiDeviceSettings;
using scenes::bind::MidiDirection;

// The ports' own indices, which `MidiDeviceSettings::zones` is ordered by.
static_assert(std::to_underlying(MidiDirection::IN) == MIDI_DIRECTION_INPUT_TO_DELUGE);
static_assert(std::to_underlying(MidiDirection::OUT) == MIDI_DIRECTION_OUTPUT_FROM_DELUGE);

// The engine's slots, which a global command and a follow input index: legacy's enums count them in the same order.
// Legacy's LAST is a boundary past the commands; the engine's array has a slot for it too.
static_assert(project::kGlobalCommandCount == util::to_underlying(GlobalMIDICommand::LAST));
static_assert(project::kGlobalCommandCount <= kNumGlobalMIDICommands);
static_assert(std::to_underlying(GlobalCommand::PLAYBACK_RESTART)
              == util::to_underlying(GlobalMIDICommand::PLAYBACK_RESTART));
static_assert(std::to_underlying(GlobalCommand::PLAY) == util::to_underlying(GlobalMIDICommand::PLAY));
static_assert(std::to_underlying(GlobalCommand::RECORD) == util::to_underlying(GlobalMIDICommand::RECORD));
static_assert(std::to_underlying(GlobalCommand::TAP) == util::to_underlying(GlobalMIDICommand::TAP));
static_assert(std::to_underlying(GlobalCommand::LOOP) == util::to_underlying(GlobalMIDICommand::LOOP));
static_assert(std::to_underlying(GlobalCommand::LOOP_CONTINUOUS_LAYERING)
              == util::to_underlying(GlobalMIDICommand::LOOP_CONTINUOUS_LAYERING));
static_assert(std::to_underlying(GlobalCommand::UNDO) == util::to_underlying(GlobalMIDICommand::UNDO));
static_assert(std::to_underlying(GlobalCommand::REDO) == util::to_underlying(GlobalMIDICommand::REDO));
static_assert(std::to_underlying(GlobalCommand::FILL) == util::to_underlying(GlobalMIDICommand::FILL));
static_assert(std::to_underlying(GlobalCommand::TRANSPOSE) == util::to_underlying(GlobalMIDICommand::TRANSPOSE));
static_assert(std::to_underlying(GlobalCommand::NEXT_SONG) == util::to_underlying(GlobalMIDICommand::NEXT_SONG));
static_assert(std::to_underlying(GlobalCommand::SHIFT) == util::to_underlying(GlobalMIDICommand::SHIFT));
static_assert(project::kFollowInputCount == kNumMIDIFollowChannelTypesIncludingTracks);
static_assert(std::to_underlying(FollowInput::A) == util::to_underlying(MIDIFollowChannelType::A));
static_assert(std::to_underlying(FollowInput::C) == util::to_underlying(MIDIFollowChannelType::C));
static_assert(std::to_underlying(FollowInput::TRACK_1) == util::to_underlying(MIDIFollowChannelType::Track1));
static_assert(std::to_underlying(FollowInput::TRACK_16) == util::to_underlying(MIDIFollowChannelType::Track16));
// The message-kind bands of a learned channel.
static_assert(project::kFirstCcChannel == IS_A_CC);
static_assert(project::kFirstProgramChangeChannel == IS_A_PC);

/// @brief @p learned as a binding, whatever it holds: its cable as a device, and its channel and number, each none
///        when unset.
LearnedBinding binding_of(const ::LearnedMIDI& learned) {
	return LearnedBinding{
	    .device = learned.cable != nullptr ? std::optional{midi::id_of(*learned.cable)} : std::nullopt,
	    .channel_or_zone = learned.containsSomething() ? std::optional<uint8_t>{learned.channelOrZone} : std::nullopt,
	    .note_or_cc =
	        learned.noteOrCC != project::MidiCommand::kNone ? std::optional<uint8_t>{learned.noteOrCC} : std::nullopt,
	};
}

/// The MIDI devices: the cables `MIDIDeviceManager` keeps, by their session handles.
class HostMidiDevices final : public scenes::bind::MidiDevices {
public:
	[[nodiscard]] std::span<const midi::DeviceId> connected() const override {
		// `Devices::getCable` and `Devices::drawPixelsForOled`
		// (`origin/main:src/deluge/gui/menu_item/midi/devices.cpp`): DIN, the first two upstream USB cables, then the
		// hosted ones in their own order, keeping only those with a connection ("Disconnected devices aren't drawn, so
		// they don't take up a row", `computeScrollForSelected`). The third upstream cable carries sysex only and is
		// never listed.
		listed_.clear();
		const auto keep = [this](const ::MIDICable& cable) {
			if (cable.connectionFlags != 0u) {
				if (const midi::DeviceId id = midi::id_of(cable)) {
					listed_.push_back(id);
				}
			}
		};
		keep(MIDIDeviceManager::dinMIDIPorts);
		keep(MIDIDeviceManager::upstreamUSBMIDICable1);
		keep(MIDIDeviceManager::upstreamUSBMIDICable2);
		for (const ::MIDICableUSBHosted* cable : MIDIDeviceManager::hostedMIDIDevices) {
			if (cable != nullptr) {
				keep(*cable);
			}
		}
		return listed_;
	}

	[[nodiscard]] std::string_view name(midi::DeviceId device) const override {
		::MIDICable* const cable = midi::cable_of(device);
		if (cable == nullptr) {
			return {};
		}
		// Static or cable-owned storage, so the view outlives the call.
		const char* const text = cable->getDisplayName();
		return text != nullptr ? std::string_view{text} : std::string_view{};
	}

	[[nodiscard]] std::optional<MidiDeviceSettings> settings(midi::DeviceId device) const override {
		const ::MIDICable* const cable = midi::cable_of(device);
		if (cable == nullptr) {
			return std::nullopt;
		}
		return settings_of(*cable);
	}

	[[nodiscard]] std::optional<project::MidiDeviceRef> reference(midi::DeviceId device) const override {
		::MIDICable* const cable = midi::cable_of(device);
		if (cable == nullptr) {
			return std::nullopt;
		}
		return bridge::reference_of(*cable);
	}

	bool write(midi::DeviceId device, const MidiDeviceSettings& settings) override {
		::MIDICable* const cable = midi::cable_of(device);
		if (cable == nullptr) {
			return false;
		}
		const MidiDeviceSettings was = settings_of(*cable);
		if (settings == was) {
			return true;
		}
		// The per-device items' `writeCurrentValue`s (`origin/main:src/deluge/gui/menu_item/midi/`): each assigns its
		// field and marks the devices to be saved.
		cable->defaultVelocityToLevel = settings.velocity_to_level;
		cable->sendClock = settings.send_clock;
		cable->receiveClock = settings.receive_clock;
		cable->is_relative = settings.relative;
		cable->midi_thru = settings.thru;
		for (size_t dir = 0; dir < settings.zones.size(); dir++) {
			cable->ports[dir].mpeLowerZoneLastMemberChannel = settings.zones[dir].lower_last_member;
			cable->ports[dir].mpeUpperZoneLastMemberChannel = settings.zones[dir].upper_last_member;
		}
		// `ZoneNumMemberChannels::writeCurrentValue`
		// (`origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`): the zone, the recount, the save
		// mark, then -- for an output -- the MCM that tells the device its zone.
		if (settings.zones != was.zones) {
			MIDIDeviceManager::recountSmallestMPEZones();
		}
		MIDIDeviceManager::anyChangesToSave = true;
		const auto out = static_cast<size_t>(std::to_underlying(MidiDirection::OUT));
		const scenes::bind::MpeZones& now = settings.zones[out];
		const scenes::bind::MpeZones& before = was.zones[out];
		if (now.lower_last_member != before.lower_last_member) {
			cable->sendRPN(0, 0, 6, now.lower_last_member);
		}
		if (now.upper_last_member != before.upper_last_member) {
			cable->sendRPN(15, 0, 6, 15 - now.upper_last_member);
		}
		return true;
	}

	[[nodiscard]] std::optional<LearnedBinding> global_command(GlobalCommand command) const override {
		// `Command::drawPixelsForOled` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`) shows a command as
		// learned only when its channel is (`containsSomething`).
		const ::LearnedMIDI& learned = midiEngine.globalMIDICommands[std::to_underlying(command)];
		return learned.containsSomething() ? std::optional{binding_of(learned)} : std::nullopt;
	}

	[[nodiscard]] std::optional<LearnedBinding> follow_input(FollowInput input) const override {
		// `FollowChannel::drawInteger` (`origin/main:src/deluge/gui/menu_item/midi/follow/follow_channel.h`) names the
		// cable whatever the channel, and the settings file can set either alone.
		const ::LearnedMIDI& learned = midiEngine.midiFollowChannelType[std::to_underlying(input)];
		if (learned.cable == nullptr && !learned.containsSomething()) {
			return std::nullopt;
		}
		return binding_of(learned);
	}

private:
	[[nodiscard]] static MidiDeviceSettings settings_of(const ::MIDICable& cable) {
		MidiDeviceSettings s{
		    .velocity_to_level = cable.defaultVelocityToLevel,
		    .send_clock = cable.sendClock,
		    .receive_clock = cable.receiveClock,
		    .relative = cable.is_relative,
		    .thru = cable.midi_thru,
		};
		for (size_t dir = 0; dir < s.zones.size(); dir++) {
			s.zones[dir] = {.lower_last_member = cable.ports[dir].mpeLowerZoneLastMemberChannel,
			                .upper_last_member = cable.ports[dir].mpeUpperZoneLastMemberChannel};
		}
		return s;
	}

	/// What `connected` last answered.
	mutable std::vector<midi::DeviceId> listed_;
};

} // namespace

scenes::bind::MidiDevices& midi_devices() {
	static HostMidiDevices devices;
	return devices;
}

} // namespace deluge::gui::shell_host
