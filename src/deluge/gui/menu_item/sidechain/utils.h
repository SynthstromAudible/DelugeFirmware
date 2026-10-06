#pragma once

#include "engines/effects/state.h"

/// The sidechain settings a sidechain menu shows: the reverb's, or the edited owner's.
[[nodiscard]] const deluge::effects::SidechainState& sidechainSettings(bool forReverb);
