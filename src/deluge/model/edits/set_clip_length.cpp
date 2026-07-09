#include "model/edits/set_clip_length.h"

#include "deluge-project/song.h" // getOrCreateDocClip
#include "model/clip/clip.h"     // bindDomClip
#include "model/edits/clip_address_for.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetClipLength::apply(Context& ctx) {
	ctx.song.setClipLength(clip_, newLength_, action_, mayReSyncClip_); // engine (authoritative, as now)

	// M3 owner-1 slice 1: additive SHADOW write into the DOM clip-structure store. Nothing reads DocClip yet — the
	// engine field stays authoritative until the read/save/load flip. Only shadow already-addressable clips; NEVER
	// mint a session id here (that would fork the shared id lifecycle — piggyback on the param tier's minted id).
	if (std::optional<project::ClipAddress> address = clipAddressFor(*clip_)) {
		project::DocClip& doc = ctx.model.getOrCreateDocClip(*address);
		doc.loop_length = static_cast<int32_t>(newLength_);
		clip_->bindDomClip(&doc); // read flip: loopLength() now resolves through the DOM store for this clip
	}
	return Error::NONE;
}

} // namespace deluge::edits
