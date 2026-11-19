#pragma once

/// @file
/// @brief A project::Song with a synth track and a sound kit row, each with an instrument slot, and fake ports
/// holding each sound's typed state.

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "effects/state.h"
#include "fakes.h"
#include "instruments/deluge/sound_state.h"
#include "scenes/bind/ports/ports.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace deluge::scenes::bind::test {

/// The synth track's slot and the kit row's.
inline constexpr project::SlotAddress kSynth{project::SlotAddress::Kind::OUTPUT, 4};
inline constexpr project::SlotAddress kDrum{project::SlotAddress::Kind::DRUM, 9};
/// A kit's chain, an audio track's chain and the master's.
inline constexpr project::ChainAddress kKitChain{project::ChainAddress::Kind::OUTPUT, 20};
inline constexpr project::ChainAddress kAudioChain{project::ChainAddress::Kind::OUTPUT, 21};
inline constexpr project::ChainAddress kMasterChain{project::ChainAddress::Kind::SONG, 0};

/// The model groups' owners: the synth track's clip; a MIDI track and its clip; a CV track on its second channel; an
/// audio track and its clip; a kit, its clip, and its sound (kDrum), MIDI and gate rows; the kit clip's sound row.
inline constexpr project::ClipAddress kSynthClip{35};
inline constexpr project::TrackAddress kMidiTrack{30};
inline constexpr project::ClipAddress kMidiClip{31};
inline constexpr project::TrackAddress kCvTrack{32};
inline constexpr project::TrackAddress kAudioTrack{33};
inline constexpr project::ClipAddress kAudioClip{34};
inline constexpr uint32_t kKit{40};
inline constexpr project::ClipAddress kKitClip{41};
inline constexpr project::LaneAddress kSoundLane{kKit, kDrum.id};
inline constexpr project::LaneAddress kMidiLane{kKit, 42};
inline constexpr project::LaneAddress kGateLane{kKit, 43};
inline constexpr project::NoteRowAddress kSoundRow{
    .clip_id = kKitClip.clip_id, .is_kit = true, .row_key = static_cast<int32_t>(kDrum.id)};
/// The device's default bend ranges the fixture's track facts give (main, MPE).
inline constexpr int32_t kDeviceBendRange = 7;
inline constexpr int32_t kDeviceBendRangeMpe = 30;

/// A song whose synth track (kSynth) and kit row (kDrum) hold the slot encode() writes for the host's sound states,
/// which start as a new synth's; whose kit (kKitChain) and audio track (kAudioChain) have a chain, as the master does,
/// each with default effects in the host's track facts; and which holds the model groups' owners above, each clip and
/// lane with an arp record.
struct ModelFixture {
	project::Song song;
	FakeHost host; ///< the fakes of the ports: each sound's typed state, the plugged inputs, each chain's effects, the
	               ///< knobs' reads
	ModelFixture();
	/// The host's ports; valid while this fixture lives.
	[[nodiscard]] Ports ports() const { return host.ports(); }
	/// Set @p slot's state and re-encode its slot from it, as a coherent song holds them.
	void set_state(project::SlotAddress slot, const instrument::SoundState& state);
};

} // namespace deluge::scenes::bind::test
