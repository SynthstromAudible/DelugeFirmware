#pragma once

#include "deluge-project/clip_address.h"
#include <optional>

class Clip;

namespace deluge::edits {

/// The durable address of a clip: its serial id when saved, else its S5-b session id, else nullopt (unaddressable).
/// The clip-tier analogue of noteRowAddressFor()/addressFor().
std::optional<deluge::project::ClipAddress> clipAddressFor(const Clip& clip);

} // namespace deluge::edits
