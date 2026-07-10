#include "model/edits/kit_row_address_for.h"

#include "model/drum/drum.h"
#include "model/instrument/kit.h"

namespace deluge::edits {

std::optional<deluge::project::KitRowAddress> kitRowAddressFor(const Kit& kit, const Drum& drum) {
	deluge::project::KitRowAddress addr{};
	if (std::optional<uint32_t> serial = kit.id()) {
		addr.kit_id = *serial;
		addr.kit_is_session = false;
	}
	else {
		return std::nullopt;
	}
	if (std::optional<uint32_t> serial = drum.id()) {
		addr.drum_id = *serial;
		addr.drum_is_session = false;
	}
	else {
		return std::nullopt;
	}
	return addr;
}

} // namespace deluge::edits
