#include "model/edits/note_address_for.h"

#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
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
	deluge::project::NoteRowAddress addr{};

	// Clip identity (durable id; clips always have one from their ctor).
	if (std::optional<uint32_t> serial = clip->id()) {
		addr.clip_id = *serial;
	}
	else {
		return std::nullopt;
	}

	// Row key: kit rows are keyed by the SOUND drum's id; melodic rows by y.
	if (clip->output->type == OutputType::KIT) {
		Drum* drum = noteRow->drum;
		if (drum == nullptr || drum->type != DrumType::SOUND) {
			return std::nullopt; // non-Sound / rowless drum — unaddressable (mirrors addressFor)
		}
		if (std::optional<uint32_t> serial = drum->id()) {
			addr.row_key = static_cast<int32_t>(*serial);
		}
		else {
			return std::nullopt;
		}
		addr.is_kit = true;
	}
	else {
		addr.row_key = noteRow->y();
		addr.is_kit = false;
	}
	return addr;
}

} // namespace deluge::edits
