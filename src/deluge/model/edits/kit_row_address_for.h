#pragma once

#include "deluge-project/kit_row_address.h"
#include <optional>

class Kit;
class Drum;

namespace deluge::edits {

/// The durable address of an existing kit row (kit + drum): serial-else-session for both, else nullopt. The kit-tier
/// analogue of clipAddressFor().
std::optional<deluge::project::KitRowAddress> kitRowAddressFor(const Kit& kit, const Drum& drum);

} // namespace deluge::edits
