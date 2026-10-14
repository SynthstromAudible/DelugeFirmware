#include "model/edits/set_clip_section.h"

#include "model/clip/clip.h"

namespace deluge::edits {

Error SetClipSection::apply(Context& /*ctx*/) {
	// Section lives in the clip's DocClip; setSection is the single writer (own store, or the tree store once
	// adopted). Not undoable.
	clip_->setSection(newSection_);
	return Error::NONE;
}

} // namespace deluge::edits
