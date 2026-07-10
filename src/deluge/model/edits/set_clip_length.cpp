#include "model/edits/set_clip_length.h"

#include "model/song/song.h"

namespace deluge::edits {

Error SetClipLength::apply(Context& ctx) {
	// M3 owner-1: clip length lives in the DocClip store. Song::setClipLength runs the resize and writes the length
	// via clip->setLoopLength(), which resolves through the clip's bound DocClip (docClip_) — the store IS the
	// authoritative record, so there is no separate shadow write.
	ctx.song.setClipLength(clip_, newLength_, action_, mayReSyncClip_);
	return Error::NONE;
}

} // namespace deluge::edits
