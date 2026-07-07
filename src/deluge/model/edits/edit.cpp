#include "model/edits/edit.h"

namespace deluge::edits {

namespace {
/// M4 (snapshot boundary) lands here: today each Edit's delegated engine call updates the engine
/// in-line, so this is a no-op; when the executor reads snapshots, this becomes the event feed.
void emitModelChanged(Edit& /*edit*/) {
}
} // namespace

Error apply(Context& ctx, Edit& edit) {
	Error error = edit.apply(ctx);
	if (error == Error::NONE) {
		emitModelChanged(edit);
	}
	return error;
}

} // namespace deluge::edits
