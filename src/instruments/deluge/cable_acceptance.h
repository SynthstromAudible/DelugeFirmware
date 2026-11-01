#pragma once

/// @file
/// @brief Which modulation sources a Deluge sound lets patch to which of its params.

#include "instruments/deluge/cables.h"
#include "instruments/deluge/sound_state.h"
#include "plugin-api/types/param.h"

namespace deluge::instrument {

/// @brief The id a source-only query names as its destination: whether @p source patches to anything at all.
inline constexpr plugin::ParamId kAnyDestination{255};

/// @brief Whether a cable from @p source to @p destination may exist on a sound in this state: the sound's settings
/// alone decide it (its synth mode, its filters' modes, its LFOs' sync, whether it is a kit's drum), never its params'
/// values.
/// @param source      The cable's source.
/// @param destination A patched param's id, or kAnyDestination.
/// @param state       The sound's typed state, its own effects' settings with it.
/// @param is_drum     Whether the sound is a kit's drum.
/// @return Whether the cable is allowed.
[[nodiscard]] bool cable_allowed(PatchSource source, plugin::ParamId destination, const SoundState& state,
                                 bool is_drum);

} // namespace deluge::instrument
