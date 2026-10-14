#pragma once

#include <string>

namespace deluge::test {

/// @brief The message of the most recent FREEZE_WITH_ERROR in this process, or empty if none; a spec clears it with
/// clear_freeze() before the call it checks.
const std::string& last_freeze();
/// @brief Forget the recorded freeze message, so last_freeze() reads empty until the next freeze.
void clear_freeze();

} // namespace deluge::test
