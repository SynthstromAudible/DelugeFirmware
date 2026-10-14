#pragma once
#include "libdeluge/board.h"
#include "shell/profile.h"
#include <cstdint>

namespace deluge::ui::shell {

/// Test board with 3 OLEDs (128×64): a descriptor for a board whose BSP does not exist yet.
extern const DelugeBoard kTestBoard;

/// The board profile for the test board: 3 OLEDs 128×64 with roles {Main, Param0, Aux}, same pads/buttons/encoders as
/// Deluge.
/// @return The profile, viewing static storage.
const BoardProfile& test_board_profile();

} // namespace deluge::ui::shell
