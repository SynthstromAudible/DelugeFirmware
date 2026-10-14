#pragma once

#include "deluge-project/kit_row_address.h"
#include <optional>

class Kit;
class Drum;

namespace deluge::edits {

/// @brief The durable address of an existing kit row (kit + drum).
///
/// The kit and the drum are each identified by their durable id. The kit-tier analogue of clipAddressFor().
///
/// @param kit  The kit holding the row.
/// @param drum The row's drum.
/// @return The row's address; nullopt if either the kit or the drum has no id.
std::optional<deluge::project::KitRowAddress> kitRowAddressFor(const Kit& kit, const Drum& drum);

} // namespace deluge::edits
