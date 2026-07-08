#include "model/edits/set_clip_section.h"

#include "deluge-project/song.h" // getOrCreateDocClip
#include "model/clip/clip.h"
#include "model/edits/clip_address_for.h"

namespace deluge::edits {

Error SetClipSection::apply(Context& ctx) {
	clip_->section = newSection_; // engine (authoritative; non-undoable, as today)

	// M3 owner-1 slice 2b: additive shadow write into the DOM clip-structure store. Only shadow already-addressable
	// clips; never mint a session id here.
	if (std::optional<project::ClipAddress> address = clipAddressFor(*clip_)) {
		ctx.model.getOrCreateDocClip(*address).section = newSection_;
	}
	return Error::NONE;
}

} // namespace deluge::edits
