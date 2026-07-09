#include "model/edits/clip_address_for.h"

#include "model/clip/clip.h"

namespace deluge::edits {

std::optional<deluge::project::ClipAddress> clipAddressFor(const Clip& clip) {
	deluge::project::ClipAddress addr{};
	// Clip identity: serial when saved, else the S5-b session id; else unaddressable (mirrors noteRowAddressFor).
	if (std::optional<uint32_t> serial = clip.id()) {
		addr.clip_id = *serial;
		addr.is_session = false;
	}
	else if (uint32_t session = clip.sessionId(); session != 0) {
		addr.clip_id = session;
		addr.is_session = true;
	}
	else {
		return std::nullopt;
	}
	return addr;
}

} // namespace deluge::edits
