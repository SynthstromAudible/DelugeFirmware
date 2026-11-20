#pragma once

/// @file
/// @brief Which modulation sources a Deluge sound lets patch to which of its params.

#include "instruments/deluge/cables.h"
#include "instruments/deluge/sound_state.h"
#include "plugin-api/types/param.h"

namespace deluge::instrument {

/// @brief The id a source-only query names as its destination: whether @p source patches to anything at all.
inline constexpr plugin::ParamId kAnyDestination{255};

/// @brief cable_allowed() for a sound's record.
[[nodiscard]] bool cable_allowed(PatchSource source, plugin::ParamId destination, const SoundState& state,
                                 bool is_drum);

} // namespace deluge::instrument
