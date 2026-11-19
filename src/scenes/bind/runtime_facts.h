#pragma once

/// @file
/// @brief The facts a menu needs that the model does not hold.

#include "controls/facts.h"
#include "deluge-project/clip_address.h"
#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/param_address.h"
#include "deluge-project/setting_group.h"
#include "effects/state.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace deluge::scenes::bind {

/// @brief What kind of track an output is.
enum class TrackKind : uint8_t { SYNTH, KIT, MIDI_OUT, CV, AUDIO };

/// @brief One of the song's tracks, as the host holds them: its id, its kind, and (MIDI and CV) its channel.
struct TrackFact {
	uint32_t output_id{0};
	TrackKind kind{TrackKind::SYNTH};
	int32_t channel{0};
	bool operator==(const TrackFact&) const = default;
};

/// @brief Whether the audio track @p editing may record from @p track: another track, and one that sounds (not a
///        MIDI or CV track), as `AudioOutput::canRecordFrom` decides.
[[nodiscard]] constexpr bool can_record_from(uint32_t editing, const TrackFact& track) {
	return track.output_id != editing && track.kind != TrackKind::MIDI_OUT && track.kind != TrackKind::CV;
}

/// @brief What an audio clip's view needs of the clip that no record holds.
struct AudioClipFact {
	uint32_t track_id{0}; ///< the audio track it plays on: the record holds no clip-to-track index
	int32_t max_zoom{0};  ///< the zoom that fits the clip's whole length, which a take being recorded sets
	bool unarmed{true};   ///< no launch or stop is armed on it
	bool reversed{false}; ///< it plays its sample reversed now: its record's reversed, inverted while it runs backwards
	int32_t session_index{-1}; ///< its index among the session's clips, by which a dirty set names clips
	bool operator==(const AudioClipFact&) const = default;
};

/// @brief The isomorphic keyboard's layout state, as the current clip keeps it (`KeyboardState::isomorphic`).
struct IsomorphicKeyboard {
	int32_t scroll_offset = 0; ///< the note on the bottom-left pad
	int32_t row_interval = 5;  ///< the semitones from one row to the next
	bool operator==(const IsomorphicKeyboard&) const = default;
};

/// @brief The facts a menu needs that are not settings, each answered by the host. Nothing else a binding reads comes
/// from outside the model. The knobs' reads (the param's live value, the stutter, note tails) are @ref
/// controls::Facts, which the host answers with the rest.
class RuntimeFacts : public controls::Facts {
public:
	virtual ~RuntimeFacts() = default;
	/// @brief The Deluge instrument's row for a sound's params, the global effectable's for a kit's, an audio track's
	/// and the song's: the rows every Deluge host's owners have.
	[[nodiscard]] const plugin::ParamInfo* param_info(plugin::PluginId plugin,
	                                                  const project::ParamTarget& target) const override;
	/// @brief The keyboard screen's isomorphic layout under the menu: its state when the root UI is the keyboard on
	/// that layout, else none. None for a host with no keyboard screen.
	[[nodiscard]] virtual std::optional<IsomorphicKeyboard> isomorphic_keyboard() const { return std::nullopt; }
	/// @brief The effects' typed state of the chain at @p chain, or nullptr when there is no such chain: what its
	/// record holds, which its owner plays.
	[[nodiscard]] virtual const effects::EffectStates* chain_effects(project::ChainAddress chain) const = 0;
	/// @brief The bend range a clip or kit row whose record holds none plays: its expression set's, made after the song
	/// loaded, or where it has none yet the one a new set starts at (the device's default, which is the user's, stored
	/// on the device; a kit row's finger range starts at the main one).
	/// @param owner The clip's (CLIP_VALUES) or row's (NOTE_ROW) group and owner.
	/// @param which 0 for the main bend range, 1 for the MPE (per-finger) one.
	[[nodiscard]] virtual int32_t played_bend_range(const project::SettingAddress& owner, int32_t which) const = 0;
	/// @brief The arp preset the engine holds for the arp @p address names (a CLIP_ARP or LANE_ARP owner), as an index
	/// of plugin::kArpPresetWords: a record derives its preset from its fields, so Custom is the engine's alone.
	[[nodiscard]] virtual uint8_t arp_preset(const project::SettingAddress& address) const = 0;
	/// @brief The song's tracks, in its order: which outputs exist, and their kinds, which the model's records do not
	/// hold.
	[[nodiscard]] virtual std::vector<TrackFact> tracks() const = 0;
	/// @brief What the audio clip @p clip's view needs of it, or nullopt where the song holds no such audio clip.
	[[nodiscard]] virtual std::optional<AudioClipFact> audio_clip(project::ClipAddress clip) const = 0;
	/// @brief Playback: whether @p clip plays now -- the transport runs, the clip is active, and no count-in is
	/// running. Launch state is the engine's, not the record's.
	[[nodiscard]] virtual bool clip_playing(project::ClipAddress clip) const = 0;
	/// @brief Playback: @p clip's play position now, in ticks: the engine's, moved by the clock.
	[[nodiscard]] virtual int32_t clip_live_pos(project::ClipAddress clip) const = 0;
	/// @brief Whether @p clip plays only in the arrangement (a take or an arranger clip), not in the session.
	[[nodiscard]] virtual bool clip_arrangement_only(project::ClipAddress clip) const = 0;
	/// @brief Recording: whether @p clip records a take linearly now, which a recorder holds while it captures.
	[[nodiscard]] virtual bool clip_recording_linearly(project::ClipAddress clip) const = 0;
	/// @brief The view of the audio clip @p clip at @p scroll and @p zoom ticks, in samples: what the waveform scan
	/// takes. A take recorded without the clock measures from what it has captured.
	/// @return The scroll and the zoom (samples per column); {0, 0} where there is no such audio clip.
	[[nodiscard]] virtual std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress clip, int32_t scroll,
	                                                                     int32_t zoom) const = 0;
};

} // namespace deluge::scenes::bind
