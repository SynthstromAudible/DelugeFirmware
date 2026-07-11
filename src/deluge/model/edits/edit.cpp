#include "model/edits/edit.h"

namespace deluge::edits {

Error apply(Context& ctx, Edit& edit) {
	Error error = edit.apply(ctx);
	if (error == Error::NONE) {
		// M4 snapshot boundary: emit the model-changed event. Per-Edit override (default no-op); additive during
		// the pilot (apply() still pokes the engine in-line). This is the first real consumer of the emission hook.
		edit.emitEvent(ctx);
	}
	return error;
}

} // namespace deluge::edits
