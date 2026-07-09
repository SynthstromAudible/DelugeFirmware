#include "model/edits/set_clip_colour.h"

#include "model/clip/clip.h"

namespace deluge::edits {

Error SetClipColour::apply(Context& /*ctx*/) {
	// M3 owner-1: colour lives in the clip's DocClip (own store, or the tree store once adopted). setColourOffset
	// writes it; no separate shadow write. Non-undoable, as today.
	clip_->setColourOffset(clip_->colourOffset() + offset_);
	return Error::NONE;
}

} // namespace deluge::edits
