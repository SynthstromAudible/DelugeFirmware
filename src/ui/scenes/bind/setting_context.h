#pragma once

/// @file
/// @brief The SettingContext a menu passes for a sound, built from its slot's address, or for a chain.

#include "plugin/types/setting.h"
#include "project/fx_chain.h"
#include "project/instrument_slot.h"
#include "project/song.h"
#include "ui/scenes/bind/ports/audio_facts.h"
#include "ui/scenes/bind/ports/device_settings.h"
#include "ui/scenes/bind/ports/sound_facts.h"
#include "ui/scenes/bind/target.h"

namespace deluge::ui::scenes::bind {

/// @brief The context a host passes for the sound at @p slot (the host contract in plugin/types/setting.h): a
/// synth track's slot is a TRACK playing a SYNTH, a kit row's a KIT_LANE playing a SOUND_DRUM, since only those hold
/// an instrument slot. context_is_coherent holds for every context this builds.
/// @param model The song.
/// @param slot  The sound's slot.
/// @param sound  The sound facts: loaded sources.
/// @param audio  The audio facts: plugged inputs.
/// @param device The device's settings, for its DX7 engine feature; null reads the feature's default (off).
/// @return The context.
[[nodiscard]] plugin::SettingContext setting_context(const project::Song& model, project::SlotAddress slot,
                                                     const SoundFacts& sound, const AudioFacts& audio,
                                                     const DeviceSettings* device);

/// @brief The context a host passes for the chain at @p chain (the host contract in plugin/types/setting.h): a
/// kit's chain is a KIT playing a KIT, an audio track's a TRACK playing AUDIO, the master's the SONG playing NONE. No
/// effect row tests inputs or loaded sources, so the context holds none.
/// @param chain The chain's address.
/// @param owner Which kind of output holds it.
/// @return The context.
[[nodiscard]] plugin::SettingContext setting_context(project::ChainAddress chain, ChainOwner owner);

/// @brief The context a host passes for @p address's group (the host contract in plugin/types/setting.h and each
/// group's in setting_group.h): the song's is the SONG playing NONE; a track's (its device, its input) a TRACK; a
/// clip's (its values, direction, arp) a CLIP; an audio clip's a CLIP playing AUDIO; a kit lane's and a kit note row's
/// a KIT_LANE playing the drum its record names. A track or clip is played by @p instrument, which the model does not
/// hold.
/// @param model      The song.
/// @param address    The group and owner.
/// @param instrument What plays the track or clip.
/// @param audio      The audio facts: plugged inputs.
/// @return The context.
[[nodiscard]] plugin::SettingContext setting_context(const project::Song& model, const project::SettingAddress& address,
                                                     plugin::SettingContext::Instrument instrument,
                                                     const AudioFacts& audio);

} // namespace deluge::ui::scenes::bind
