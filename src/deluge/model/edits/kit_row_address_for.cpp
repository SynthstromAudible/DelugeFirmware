#include "model/edits/kit_row_address_for.h"

#include "model/drum/drum.h"
#include "model/instrument/kit.h"

namespace deluge::edits {

std::optional<deluge::project::KitRowAddress> kitRowAddressFor(const Kit& kit, const Drum& drum) {
	deluge::project::KitRowAddress addr{};
	if (std::optional<uint32_t> serial = kit.serialId()) {
		addr.kit_id = *serial;
		addr.kit_is_session = false;
	}
	else if (uint32_t session = kit.sessionId(); session != 0) {
		addr.kit_id = session;
		addr.kit_is_session = true;
	}
	else {
		return std::nullopt;
	}
	if (std::optional<uint32_t> serial = drum.serialId()) {
		addr.drum_id = *serial;
		addr.drum_is_session = false;
	}
	else if (uint32_t session = drum.sessionId(); session != 0) {
		addr.drum_id = session;
		addr.drum_is_session = true;
	}
	else {
		return std::nullopt;
	}
	return addr;
}

} // namespace deluge::edits
