#include "model/edits/set_clip_colour.h"

#include "model/clip/clip.h"
#include "model/song/song.h"

namespace deluge::edits {

Error SetClipColour::apply(Context& ctx) {
	Clip* clip = ctx.song.clipForId(clip_.clip_id);
	if (clip == nullptr) {
		return Error::UNSPECIFIED;
	}
	// Colour lives in the clip's DocClip (own store, or the tree store once adopted); setColourOffset writes it.
	// Not undoable.
	clip->setColourOffset(clip->colourOffset() + offset_);
	return Error::NONE;
}

} // namespace deluge::edits
