#pragma once

/// @file
/// @brief The facts a menu needs that the model does not hold.

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/setting_group.h"
#include "engines/deluge/sound_state.h"
#include "engines/effects/state.h"

#include <cstdint>

namespace deluge::scenes::bind {

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
	/// @brief The effects' typed state of the chain at @p chain, or nullptr when there is no such chain. It is what the
	/// chain's record decodes to, kept equal to it by every Edit.
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
};

} // namespace deluge::scenes::bind
