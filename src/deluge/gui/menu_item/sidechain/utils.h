#pragma once

#include "engines/effects/state.h"

/// @brief The sidechain settings a sidechain menu shows: the reverb's, or the edited owner's.
/// @param forReverb True for the reverb's sidechain, false for the edited owner's.
/// @return The sidechain state to display.
[[nodiscard]] const deluge::effects::SidechainState& sidechainSettings(bool forReverb);
