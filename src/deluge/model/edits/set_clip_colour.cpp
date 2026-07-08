#include "model/edits/set_clip_colour.h"

#include "deluge-project/song.h" // getOrCreateDocClip
#include "model/clip/clip.h"
#include "model/edits/clip_address_for.h"

namespace deluge::edits {

Error SetClipColour::apply(Context& ctx) {
	clip_->colourOffset += offset_; // engine (authoritative; non-undoable, as today)

	// M3 owner-1 slice 2: additive shadow write of the resulting absolute offset into the DOM clip-structure store.
	// Only shadow already-addressable clips; never mint a session id here.
	if (std::optional<project::ClipAddress> address = clipAddressFor(*clip_)) {
		ctx.model.getOrCreateDocClip(*address).colour_offset = clip_->colourOffset;
	}
	return Error::NONE;
}

} // namespace deluge::edits
