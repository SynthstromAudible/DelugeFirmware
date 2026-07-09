#include "model/edits/set_clip_launch_style.h"

#include "model/clip/clip.h"

namespace deluge::edits {

Error SetClipLaunchStyle::apply(Context& /*ctx*/) {
	// M3 owner-1: launch style lives in the clip's DocClip; setLaunchStyle is the single writer (own store, or the
	// tree store once adopted). Non-undoable, as today.
	clip_->setLaunchStyle(newStyle_);
	return Error::NONE;
}

} // namespace deluge::edits
