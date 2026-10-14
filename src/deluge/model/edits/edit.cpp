#include "model/edits/edit.h"

namespace deluge::edits {

Error apply(Context& ctx, Edit& edit) {
	Error error = edit.apply(ctx);
	if (error == Error::NONE) {
		// Snapshot boundary: emit the model-changed event. Per-Edit override (default no-op); apply() still pokes
		// the engine in-line, so the event is a parallel consumer.
		edit.emitEvent(ctx);
	}
	return error;
}

} // namespace deluge::edits
