#include "model/edits/edit_clip_instance.h"

#include "model/clip/clip_instance.h"

namespace deluge::edits {

Error EditClipInstance::apply(Context& /*ctx*/) {
	clipInstance_->change(action_, output_, newPos_, newLength_, newClip_);
	return Error::NONE;
}

} // namespace deluge::edits
