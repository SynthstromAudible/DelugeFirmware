#include "model/edits/set_clip_length.h"

#include "model/song/song.h"

namespace deluge::edits {

Error SetClipLength::apply(Context& ctx) {
	ctx.song.setClipLength(clip_, newLength_, action_, mayReSyncClip_);
	return Error::NONE;
}

} // namespace deluge::edits
