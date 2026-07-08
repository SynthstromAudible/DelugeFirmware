#include "model/edits/remove_kit_row.h"

#include "deluge-project/param_address.h" // OwnerId
#include "deluge-project/song.h"          // project: param_backup / backed_params
#include "memory/general_memory_allocator.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"

namespace deluge::edits {

// Verbatim reification of InstrumentClipView::deleteDrum (non-undoable — no Consequence): remove the drum, reassign the
// row to an unassigned drum (or delete an empty row), retire the drum's automation stores, then destruct + dealloc it.
Error RemoveKitRow::apply(Context& ctx) {
	auto* clip = static_cast<InstrumentClip*>(engineStack_->getTimelineCounter());
	auto* kit = static_cast<Kit*>(clip->output);

	kit->removeDrum(drum_);

	// Find Drum's NoteRow
	int32_t noteRowIndex;
	NoteRow* noteRow = clip->getNoteRowForDrum(drum_, &noteRowIndex);
	if (noteRow) {
		ModelStackWithNoteRow* modelStackWithNoteRow = engineStack_->addNoteRow(noteRowIndex, noteRow);

		// Give NoteRow another unassigned Drum, or no Drum if there are none
		noteRow->setDrum(kit->getFirstUnassignedDrum(clip), kit, modelStackWithNoteRow);

		if (!noteRow->drum) {
			// If NoteRow has no Notes, just delete it - if it's not the last one
			if (noteRow->hasNoNotes() && clip->getNumNoteRows() > 1) {
				if (noteRowIndex == 0) {
					clip->yScroll--;
				}
				clip->deleteNoteRow(engineStack_, noteRowIndex);
			}
		}
	}

	// Delete the Drum we came here to delete
	ctx.model.param_backup.deleteForModControllable(drum_);
	if (std::optional<uint32_t> drumId = drum_->serialId()) { // retire its tree-resident automation stores
		ctx.model.backed_params.removeOwner(deluge::project::OwnerId::drum(*drumId));
	}
	void* toDealloc = dynamic_cast<void*>(drum_);
	drum_->~SoundDrum();
	delugeDealloc(toDealloc);

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	return Error::NONE;
}

} // namespace deluge::edits
