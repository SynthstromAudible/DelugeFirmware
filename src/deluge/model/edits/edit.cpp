#include "model/edits/edit.h"
#include "modulation/automation/record_events.h"

namespace deluge::edits {

Error apply(Context& ctx, Edit& edit) {
	deluge::modulation::drainRecordEvents(ctx.song); // an Edit sees, and lands after, every value recorded
	Error error = edit.apply(ctx);
	if (error == Error::NONE) {
		// Snapshot boundary: emit the model-changed event. Per-Edit override (default no-op); apply() still pokes
		// the engine in-line, so the event is a parallel consumer.
		edit.emitEvent(ctx);
	}
	return error;
}

} // namespace deluge::edits
