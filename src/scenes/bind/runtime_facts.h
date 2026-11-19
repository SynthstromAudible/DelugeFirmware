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

/// @brief `PadLEDs::flashCursor`'s three values (`FLASH_CURSOR_*`).
enum class FlashCursor : uint8_t {
	/// `FLASH_CURSOR_FAST` (0): the PIC flashes the pad; no pixel of the rendered image changes, so no cursor is drawn.
	Fast = 0,
	/// `FLASH_CURSOR_OFF` (1): no playhead.
	Off = 1,
	/// `FLASH_CURSOR_SLOW` (2): the factory default, and the one mode `PadLEDs::prepareColour` substitutes a pixel in.
	Slow = 2,
};

/// @brief The performance view's editing mode, numbered as `PerformanceEditingMode` (`definitions_cxx.hpp`), which this
/// library may not include.
enum class PerformanceEditing : uint8_t {
	Disabled = 0, ///< not editing: the view's pads play its FX columns
	Value = 1,    ///< editing the columns' values
	Param = 2,    ///< editing which param each column holds
};

/// @brief The DX7 bank a `.syx` read loaded: its programs' names, as `DX7Cartridge::getProgramNames` gives them.
struct DxCartridgeNames {
	std::array<std::array<char, 11>, 32> names{}; ///< each program's name, NUL-terminated
	int32_t count = 0;                            ///< how many programs the bank holds
};

/// @brief The isomorphic keyboard's layout state, as the current clip keeps it (`KeyboardState::isomorphic`).
struct IsomorphicKeyboard {
	int32_t scroll_offset = 0; ///< the note on the bottom-left pad
	int32_t row_interval = 5;  ///< the semitones from one row to the next
	bool operator==(const IsomorphicKeyboard&) const = default;
};

/// @brief What a recording is taken for, each part by the identity that names that object alone: the take attaches
/// there, or nowhere.
struct TakeTarget {
	std::optional<project::ClipAddress> clip; ///< the clip the take was begun over
	project::SlotAddress slot{};              ///< the sound: a synth track's output, or a kit row's drum
	uint8_t source{0};                        ///< which of the sound's two sources
	std::optional<uint64_t> range;            ///< the range's identity (`RuntimeFacts::range_identity`)
	bool operator==(const TakeTarget&) const = default;
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
	/// @brief Hardware: whether a mic or line input is plugged in. No record holds it; it changes under the song.
	[[nodiscard]] virtual bool audio_inputs_present() const = 0;
	/// @brief Audio: the voices sounding now, synth and audio-clip (`cpu::VoiceUsage::sample`,
	/// `cpu/voice_usage.h:94-96`). No record holds it; it changes every render. Zero for a host that renders nothing.
	[[nodiscard]] virtual int32_t live_voice_count() const { return 0; }
	/// @brief Whether the sound at @p slot is a kit's only drum, on its clip's first note row: the kit a whole folder
	/// of samples may be loaded into, one drum per file (`SampleBrowser::canImportWholeKit`, which also asks that the
	/// menu edits source 0 -- the caller's to check).
	[[nodiscard]] virtual bool kit_takes_whole_folder(project::SlotAddress /*slot*/) const { return false; }
	/// @brief Whether an explicit sample load or a folder import is running: either one closes the sample browser to
	/// the presses that would start another. False for a host that loads nothing.
	[[nodiscard]] virtual bool sample_load_in_flight() const { return false; }
	/// @brief Whether a note is being auditioned: legacy's `currentUIMode` holds `UI_MODE_AUDITIONING`, a mode the
	/// marker editor's gates read. False for a host that auditions nothing.
	[[nodiscard]] virtual bool auditioning() const { return false; }
	/// @brief Whether a popup or notification is up: legacy's `display->hasPopup()`, which the note-range picker's
	/// turns and its delete wait out. False for a host that shows none.
	[[nodiscard]] virtual bool popup_showing() const { return false; }
	/// @brief The range source @p source of the sound at @p slot remembers: `Source::defaultRangeI`, which the
	/// note-range picker opens on and every note played moves (`Source::getRange`). None when it names no range.
	[[nodiscard]] virtual std::optional<uint16_t> default_range(project::SlotAddress slot, uint8_t source) const {
		(void)slot;
		(void)source;
		return std::nullopt;
	}
	/// @brief The identity of range @p range of source @p source of the sound at @p slot (`MultiRange::identity`). It
	/// names the range itself: it moves with the range as ranges are inserted or deleted before it, and stays while
	/// another file is loaded into it. Never 0: identities count from 1, so 0 names no range. None where the address
	/// names no range.
	[[nodiscard]] virtual std::optional<uint64_t> range_identity(project::SlotAddress slot, uint8_t source,
	                                                             uint16_t range) const {
		(void)slot;
		(void)source;
		(void)range;
		return std::nullopt;
	}
	/// @brief Where the take @p target names may still land: the index its range now has among its source's ranges,
	/// while the clip is still in the current song, still playing the sound. Range identities are run-unique and never
	/// saved, so a range in another song never matches. None otherwise, and for a host that records nothing.
	[[nodiscard]] virtual std::optional<uint16_t> take_range(const TakeTarget& target) const {
		(void)target;
		return std::nullopt;
	}
	/// @brief Whether a load that can replace an output or a sound's sample is running: an instrument, pattern, song,
	/// drum-randomizer, sample-claim, folder-import or reversion load. A recording does not start over one: what it
	/// would attach to could be swapped out from under it. False for a host that loads nothing.
	[[nodiscard]] virtual bool output_may_be_replaced() const { return false; }
	/// @brief The DX7 bank the last `.syx` read loaded, which the cartridge page lists; none before one has loaded or
	/// for a host that reads none.
	[[nodiscard]] virtual std::optional<DxCartridgeNames> dx_cartridge() const { return std::nullopt; }
	/// @brief Whether the DX7 engine community feature is on (`RuntimeFeatureSettingType::EnableDX7Engine`), which
	/// gives the DX7 parameter page its own pad layout. Off for a host that keeps no settings.
	[[nodiscard]] virtual bool dx7_engine_enabled() const { return false; }
	/// @brief The keyboard screen's isomorphic layout under the menu: its state when the root UI is the keyboard on
	/// that layout, else none. None for a host with no keyboard screen.
	[[nodiscard]] virtual std::optional<IsomorphicKeyboard> isomorphic_keyboard() const { return std::nullopt; }
	/// @brief The flash-cursor setting (`PadLEDs::flashCursor`, flash byte 36): how a playhead is shown. The factory's
	/// Slow for a host that keeps none.
	[[nodiscard]] virtual FlashCursor flash_cursor() const { return FlashCursor::Slow; }
	/// @brief The performance view's editing mode, as `EditingMode::readCurrentValue` reads it: its
	/// `defaultEditingMode`, then its `editingParam`. The performance-editing page's cursor starts on it. Disabled for
	/// a host with no performance view.
	[[nodiscard]] virtual PerformanceEditing performance_editing_mode() const { return PerformanceEditing::Disabled; }
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
	/// @brief The arp preset the engine holds for the arp @p address names (a CLIP_ARP or LANE_ARP owner), as an index
	/// of plugin::kArpPresetWords: a record derives its preset from its fields, so Custom is the engine's alone.
	[[nodiscard]] virtual uint8_t arp_preset(const project::SettingAddress& address) const = 0;
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
	/// @brief The view of the audio clip @p clip at @p scroll and @p zoom ticks, in samples: what the waveform scan
	/// takes. A take recorded without the clock measures from what it has captured.
	/// @return The scroll and the zoom (samples per column); {0, 0} where there is no such audio clip.
	[[nodiscard]] virtual std::pair<int64_t, int64_t> audio_clip_samples(project::ClipAddress clip, int32_t scroll,
	                                                                     int32_t zoom) const = 0;
};

} // namespace deluge::scenes::bind
