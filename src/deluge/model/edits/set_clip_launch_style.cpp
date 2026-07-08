#include "model/edits/set_clip_launch_style.h"

#include "deluge-project/song.h" // getOrCreateDocClip
#include "model/clip/clip.h"
#include "model/edits/clip_address_for.h"

namespace deluge::edits {

Error SetClipLaunchStyle::apply(Context& ctx) {
	clip_->launchStyle = newStyle_; // engine (authoritative; non-undoable, as today)

	// M3 owner-1 slice 2: additive shadow write into the DOM clip-structure store (see SetClipLength). Only shadow
	// already-addressable clips; never mint a session id here.
	if (std::optional<project::ClipAddress> address = clipAddressFor(*clip_)) {
		ctx.model.getOrCreateDocClip(*address).launch_style = static_cast<uint8_t>(newStyle_);
	}
	return Error::NONE;
}

} // namespace deluge::edits
