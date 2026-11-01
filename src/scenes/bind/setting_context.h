#pragma once

/// @file
/// @brief The SettingContext a menu passes for a sound, built from its slot's address, or for a chain.

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/song.h"
#include "plugin-api/types/setting.h"
#include "scenes/bind/runtime_facts.h"
#include "scenes/bind/target.h"

namespace deluge::scenes::bind {

/// @brief The context a host passes for the sound at @p slot (the host contract in plugin-api/types/setting.h): a
/// synth track's slot is a TRACK playing a SYNTH, a kit row's a KIT_LANE playing a SOUND_DRUM, since only those hold
/// an instrument slot. context_is_coherent holds for every context this builds.
/// @param model The song.
/// @param slot  The sound's slot.
/// @param facts The host's runtime facts: plugged inputs and loaded sources.
/// @return The context.
[[nodiscard]] plugin::SettingContext setting_context(const project::Song& model, project::SlotAddress slot,
                                                     const RuntimeFacts& facts);

/// @brief The context a host passes for the chain at @p chain (the host contract in plugin-api/types/setting.h): a
/// kit's chain is a KIT playing a KIT, an audio track's a TRACK playing AUDIO, the master's the SONG playing NONE. No
/// effect row tests inputs or loaded sources, so the context holds none.
/// @param chain The chain's address.
/// @param owner Which kind of output holds it.
/// @return The context.
[[nodiscard]] plugin::SettingContext setting_context(project::ChainAddress chain, ChainOwner owner);

/// @brief The context a host passes for @p address's group (the host contract in plugin-api/types/setting.h and each
/// group's in setting_group.h): the song's is the SONG playing NONE; a track's (its device, its input) a TRACK; a
/// clip's (its values, direction, arp) a CLIP; an audio clip's a CLIP playing AUDIO; a kit lane's and a kit note row's
/// a KIT_LANE playing the drum its record names. A track or clip is played by @p instrument, which the model does not
/// hold.
/// @param model      The song.
/// @param address    The group and owner.
/// @param instrument What plays the track or clip.
/// @param facts      The host's runtime facts: plugged inputs.
/// @return The context.
[[nodiscard]] plugin::SettingContext setting_context(const project::Song& model, const project::SettingAddress& address,
                                                     plugin::SettingContext::Instrument instrument,
                                                     const RuntimeFacts& facts);

} // namespace deluge::scenes::bind
