#pragma once

#include "deluge-project/clip_address.h"
#include <optional>

class Clip;

namespace deluge::edits {

/// @brief The durable address of a clip.
///
/// Every clip has a durable id from construction, so this always resolves; the optional mirrors noteRowAddressFor().
/// The clip-tier analogue of noteRowAddressFor() and addressFor().
///
/// @param clip The clip to address.
/// @return The clip's address (never nullopt).
std::optional<deluge::project::ClipAddress> clipAddressFor(const Clip& clip);

} // namespace deluge::edits
