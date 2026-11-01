#pragma once

/// @file
/// @brief The facts a menu needs that the model does not hold.

#include "deluge-project/clip_address.h"
#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/param_address.h"
#include "deluge-project/setting_group.h"
#include "effects/state.h"
#include "instruments/deluge/cables.h"
#include "instruments/deluge/sound_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace deluge::scenes::bind {

/// @brief One of a sound's patch cables, as the engine holds it.
struct CableFact {
	plugin::ParamId depth;           ///< the cable's depth id, which names its source and its destination
	instrument::Polarity polarity{}; ///< its polarity
};

/// @brief The most patch cables a sound holds.
inline constexpr size_t kMaxCables = 32;

/// @brief A sound's patch cables, as the engine holds them, in its order.
struct CableList {
	std::array<CableFact, kMaxCables> items{};
	size_t count{0};
	[[nodiscard]] const CableFact* begin() const { return items.data(); }
	[[nodiscard]] const CableFact* end() const { return items.data() + count; }
	[[nodiscard]] size_t size() const { return count; }
	[[nodiscard]] bool empty() const { return count == 0; }
	[[nodiscard]] const CableFact& operator[](size_t i) const { return items[i]; }
};

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

/// @brief The facts a menu needs that are not settings, each answered by the host. Nothing else a binding reads comes
/// from outside the model.
class RuntimeFacts {
public:
	virtual ~RuntimeFacts() = default;
	/// @brief Hardware: whether a mic or line input is plugged in. No record holds it; it changes under the song.
	[[nodiscard]] virtual bool audio_inputs_present() const = 0;
	/// @brief Audio: the voices sounding now, synth and audio-clip (`cpu::VoiceUsage::sample`,
	/// `cpu/voice_usage.h:94-96`). No record holds it; it changes every render. Zero for a host that renders nothing.
	[[nodiscard]] virtual int32_t live_voice_count() const { return 0; }
	/// @brief Storage: which sources of the sound at @p slot have an audio file loaded (bit i for source i). A file
	/// loads after the song that names it, so whether it has is a fact of the card, not of the song.
	[[nodiscard]] virtual uint8_t loaded_sources(project::SlotAddress slot) const = 0;
	/// @brief The instrument's typed state for the sound at @p slot, or nullptr when there is no such sound. It is what
	/// the slot decodes to, kept equal to it by every Edit, plus what no file can hold (an FM sound's oscillator types,
	/// a non-FM sound's modulators): a read of a row a menu does not show, which a condition may still read, needs it.
	[[nodiscard]] virtual const instrument::SoundState* sound_state(project::SlotAddress slot) const = 0;
	/// @brief The effects' typed state of the chain at @p chain, or nullptr when there is no such chain: what its
	/// record holds, which its owner plays.
	[[nodiscard]] virtual const effects::EffectStates* chain_effects(project::ChainAddress chain) const = 0;
	/// @brief The bend range a clip or kit row whose record holds none plays: its expression set's, made after the song
	/// loaded, or where it has none yet the one a new set starts at (the device's default, which is the user's, stored
	/// on the device; a kit row's finger range starts at the main one).
	/// @param owner The clip's (CLIP_VALUES) or row's (NOTE_ROW) group and owner.
	/// @param which 0 for the main bend range, 1 for the MPE (per-finger) one.
	[[nodiscard]] virtual int32_t played_bend_range(const project::SettingAddress& owner, int32_t which) const = 0;
	/// @brief Whether the sound drum at @p slot can hold note tails, which its kit-arp inclusion needs: its params
	/// (envelope sustain, noise) decide it, which no setting holds.
	[[nodiscard]] virtual bool allows_note_tails(project::SlotAddress slot) const = 0;
	/// @brief The arp preset the engine holds for the arp @p address names (a CLIP_ARP or LANE_ARP owner), as an index
	/// of plugin::kArpPresetWords: a record derives its preset from its fields, so Custom is the engine's alone.
	[[nodiscard]] virtual uint8_t arp_preset(const project::SettingAddress& address) const = 0;
	/// @brief The value the param at @p address plays now: the engine's cursor, which automation moves at the
	/// playhead, a latch or override holds, and stutter-type effects and recording change, where the record holds the
	/// saved base value; nullopt where the song holds no such param.
	[[nodiscard]] virtual std::optional<int32_t> param_value(const project::ParamAddress& address) const = 0;
	/// @brief @p address's value at @p pos in its clip, as its automation reads there (the take's while the
	/// arrangement records the clip, in the clip's or the note row's own loop); its current value at 0xFFFFFFFF.
	[[nodiscard]] virtual std::optional<int32_t> param_value_at(const project::ParamAddress& address,
	                                                            uint32_t pos) const = 0;
	/// @brief The patch cables of the sound @p owner names (a synth or a sound drum, with its clip; the id is not
	/// read), in the order the engine holds them; empty where there is no such sound. The cables are its engine's: one
	/// made this session is one before the song is saved.
	[[nodiscard]] virtual CableList cables(const project::ParamAddress& owner) const = 0;
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
	/// @brief Whether a stutter is playing: the song's stutter, which a gold knob's push begins, is live state no
	/// record holds.
	[[nodiscard]] virtual bool stutter_active() const = 0;
	/// @brief The view of the audio clip @p clip at @p scroll and @p zoom ticks, in samples: what the waveform scan
	/// takes. A take recorded without the clock measures from what it has captured.
	/// @return The scroll and the zoom (samples per column); {0, 0} where there is no such audio clip.
	[[nodiscard]] virtual std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress clip, int32_t scroll,
	                                                                     int32_t zoom) const = 0;
};

} // namespace deluge::scenes::bind
