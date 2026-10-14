#include "model/edits/set_clip_colour.h"

#include "model/clip/clip.h"

namespace deluge::edits {

Error SetClipColour::apply(Context& /*ctx*/) {
	// Colour lives in the clip's DocClip (own store, or the tree store once adopted); setColourOffset writes it.
	// Not undoable.
	clip_->setColourOffset(clip_->colourOffset() + offset_);
	return Error::NONE;
}

} // namespace deluge::edits
