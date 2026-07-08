#include "model/edits/paste_note_row.h"

#include "model/model_stack.h"
#include "model/note/note_row.h"

namespace deluge::edits {

Error PasteNoteRow::apply(Context& /*ctx*/) {
	bool ok = engineStack_->getNoteRow()->paste(engineStack_, copiedRow_, scaleFactor_, screenEndPos_, action_);
	return ok ? Error::NONE : Error::INSUFFICIENT_RAM;
}

} // namespace deluge::edits
