#pragma once

/// @file
/// @brief The facts a menu needs that the model does not hold.

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/param_address.h"
#include "deluge-project/setting_group.h"
#include "engines/deluge/cables.h"
#include "engines/deluge/sound_state.h"
#include "engines/effects/state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace deluge::scenes::bind {

/// @brief One of a sound's patch cables, as the engine holds it.
struct CableFact {
	engine::ParamId depth;           ///< the cable's depth id, which names its source and its destination
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

/// @brief The facts a menu needs that are not settings, each answered by the host. Nothing else a binding reads comes
/// from outside the model.
class RuntimeFacts {
public:
	virtual ~RuntimeFacts() = default;
	/// @brief Hardware: whether a mic or line input is plugged in. No record holds it; it changes under the song.
	[[nodiscard]] virtual bool audio_inputs_present() const = 0;
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
	/// of engine::kArpPresetWords: a record derives its preset from its fields, so Custom is the engine's alone.
	[[nodiscard]] virtual uint8_t arp_preset(const project::SettingAddress& address) const = 0;
	/// @brief The value the param at @p address plays now: the engine's cursor, which automation moves at the
	/// playhead, a latch or override holds, and stutter-type effects and recording change, where the record holds the
	/// saved base value; nullopt where the song holds no such param.
	[[nodiscard]] virtual std::optional<int32_t> param_value(const project::ParamAddress& address) const = 0;
	/// @brief The patch cables of the sound @p owner names (a synth or a sound drum, with its clip; the id is not
	/// read), in the order the engine holds them; empty where there is no such sound. The cables are its engine's: one
	/// made this session is one before the song is saved.
	[[nodiscard]] virtual CableList cables(const project::ParamAddress& owner) const = 0;
};

} // namespace deluge::scenes::bind
