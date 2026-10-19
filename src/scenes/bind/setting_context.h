#pragma once

/// @file
/// @brief The SettingContext a menu passes for a sound, built from its slot's address.

#include "deluge-project/instrument_slot.h"
#include "deluge-project/song.h"
#include "engine-api/types/setting.h"
#include "scenes/bind/runtime_facts.h"

namespace deluge::scenes::bind {

/// @brief The context a host passes for the sound at @p slot (the host contract in engine-api/types/setting.h): a
/// synth track's slot is a TRACK playing a SYNTH, a kit row's a KIT_LANE playing a SOUND_DRUM, since only those hold
/// an instrument slot. context_is_coherent holds for every context this builds.
/// @param model The song.
/// @param slot  The sound's slot.
/// @param facts The host's runtime facts: plugged inputs and loaded sources.
/// @return The context.
[[nodiscard]] engine::SettingContext setting_context(const project::Song& model, project::SlotAddress slot,
                                                     const RuntimeFacts& facts);

} // namespace deluge::scenes::bind
