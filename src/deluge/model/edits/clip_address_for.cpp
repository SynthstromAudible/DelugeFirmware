#include "model/edits/clip_address_for.h"

#include "model/clip/clip.h"

namespace deluge::edits {

std::optional<deluge::project::ClipAddress> clipAddressFor(const Clip& clip) {
	// Every clip has a durable id from construction, so this always resolves. The optional return mirrors
	// noteRowAddressFor's shape.
	return deluge::project::ClipAddress{clip.id()};
}

} // namespace deluge::edits
