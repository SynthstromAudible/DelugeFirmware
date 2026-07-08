#include "model/edits/note_address_for.h"

#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"

namespace deluge::edits {

std::optional<deluge::project::NoteRowAddress> noteRowAddressFor(const ModelStackWithNoteRow& modelStack) {
	auto* clip = static_cast<InstrumentClip*>(modelStack.getTimelineCounterAllowNull());
	NoteRow* noteRow = modelStack.getNoteRowAllowNull();
	if (clip == nullptr || noteRow == nullptr || clip->output == nullptr) {
		return std::nullopt;
	}
	// Melodic-only: kit note-rows key on the drum id (M2c), not y.
	if (clip->output->type == OutputType::KIT) {
		return std::nullopt;
	}
	deluge::project::NoteRowAddress addr{};
	if (std::optional<uint32_t> serial = clip->serialId()) {
		addr.clip_id = *serial;
		addr.clip_is_session = false;
	}
	else if (uint32_t session = clip->sessionId(); session != 0) {
		addr.clip_id = session;
		addr.clip_is_session = true;
	}
	else {
		return std::nullopt; // unaddressable clip
	}
	addr.y = noteRow->y;
	return addr;
}

} // namespace deluge::edits
