#pragma once

/// @file
/// @brief What a scene reads of the song's tracks and owners that their records do not hold.

#include "deluge-project/fx_chain.h"
#include "deluge-project/setting_group.h"
#include "effects/state.h"

#include <cstdint>
#include <vector>

namespace deluge::scenes::bind {

/// @brief What kind of track an output is.
enum class TrackKind : uint8_t {
	SYNTH,    ///< a synth track
	KIT,      ///< a kit track
	MIDI_OUT, ///< a MIDI track, playing an external device
	CV,       ///< a CV/gate track
	AUDIO,    ///< an audio track
};

/// @brief One of the song's tracks, as the host holds them: its id, its kind, and (MIDI and CV) its channel.
struct TrackFact {
	uint32_t output_id{0};            ///< the track's output id
	TrackKind kind{TrackKind::SYNTH}; ///< what kind of track it is
	int32_t channel{0};               ///< MIDI and CV only: the channel it plays on
	bool operator==(const TrackFact&) const = default;
};

/// @brief Whether the audio track @p editing may record from @p track: another track, and one that sounds (not a
///        MIDI or CV track), as `AudioOutput::canRecordFrom` decides.
[[nodiscard]] constexpr bool can_record_from(uint32_t editing, const TrackFact& track) {
	return track.output_id != editing && track.kind != TrackKind::MIDI_OUT && track.kind != TrackKind::CV;
}

/// @brief The song's track and owner facts, answered by the host.
class TrackFacts {
public:
	virtual ~TrackFacts() = default;
	/// @brief The song's tracks, in its order: which outputs exist, and their kinds, which the model's records do not
	/// hold.
	[[nodiscard]] virtual std::vector<TrackFact> tracks() const = 0;
	/// @brief The bend range a clip or kit row whose record holds none plays: its expression set's, made after the song
	/// loaded, or where it has none yet the one a new set starts at (the device's default, which is the user's, stored
	/// on the device; a kit row's finger range starts at the main one).
	/// @param owner The clip's (CLIP_VALUES) or row's (NOTE_ROW) group and owner.
	/// @param which 0 for the main bend range, 1 for the MPE (per-finger) one.
	/// @return The bend range, in semitones.
	[[nodiscard]] virtual int32_t played_bend_range(const project::SettingAddress& owner, int32_t which) const = 0;
	/// @brief The arp preset the engine holds for the arp @p address names (a CLIP_ARP or LANE_ARP owner), as an index
	/// of plugin::kArpPresetWords: a record derives its preset from its fields, so Custom is the engine's alone.
	/// @param address The arp's group and owner.
	/// @return The preset's index.
	[[nodiscard]] virtual uint8_t arp_preset(const project::SettingAddress& address) const = 0;
	/// @brief The effects' typed state of the chain at @p chain, or nullptr when there is no such chain: what its
	/// record holds, which its owner plays.
	/// @param chain The chain's address.
	/// @return The effects' state, or nullptr.
	[[nodiscard]] virtual const effects::EffectStates* chain_effects(project::ChainAddress chain) const = 0;
};

} // namespace deluge::scenes::bind
