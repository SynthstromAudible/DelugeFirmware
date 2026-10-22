#include "model/edits/remove_kit_row.h"

#include "deluge-project/param_address.h" // OwnerId
#include "deluge-project/song.h"          // project: param_backup / backed_params
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_drum.h"

namespace deluge::edits {

// Non-undoable (no Consequence): end the drum's note and take it out of the render's set, remove it, reassign the
// row to an unassigned drum (or delete an empty row), retire the drum's automation stores, then hand the drum to the
// render to let go of and free.
Error RemoveKitRow::apply(Context& ctx) {
	auto* clip = static_cast<InstrumentClip*>(engineStack_->getTimelineCounter());
	auto* kit = static_cast<Kit*>(clip->output);

	// Find Drum's NoteRow
	int32_t noteRowIndex;
	NoteRow* noteRow = clip->getNoteRowForDrum(drum_, &noteRowIndex);

	// The drum may still be sounding (an audition pad held through LOAD/KIT and back), and the kit's render only
	// ever reaches a drum in its renderingDrums: end its note and take it out of that set before removing
	// it, as the preset-replace path (LoadInstrumentPresetUI::swapKitRow) does. Its voices are ended by the render
	// when it lets go of the retired drum, below.
	// TODO: renderingDrums is the render's set, written here from the interaction tier (swapKitRow does
	// the same). A cross-tier write, left for the interaction-side voice/render-set commands.
	ModelStackWithNoteRow* modelStackWithNoteRow =
	    (noteRow != nullptr) ? engineStack_->addNoteRow(noteRowIndex, noteRow) : nullptr;
	if (noteRow) {
		noteRow->stopCurrentlyPlayingNote(modelStackWithNoteRow);
	}
	kit->renderingDrums.remove(drum_);

	kit->removeDrum(drum_);

	if (noteRow) {
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

	// Delete the Drum we came here to delete: the render ends its voices and lets go of it before it is freed
	ctx.song.live.param_backup.deleteFor(drum_);
	if (std::optional<uint32_t> drumId = drum_->id()) { // retire its tree-resident automation stores
		ctx.model.backed_params.removeOwner(deluge::project::OwnerId::drum(*drumId));
	}
	// Out of the kit for good: the render's let-go of the drum must not write back into the kit's
	// renderingDrums, which may be freed by then (SoundDrum::setSkippingRendering skips a null kit). Erase
	// again first: a sequencer note-on between the first erase and the note row's reassignment re-inserts the drum,
	// and after this nothing can name it.
	kit->renderingDrums.remove(drum_);
	drum_->kit = nullptr;
	AudioEngine::retire(drum_);

	AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;
	return Error::NONE;
}

} // namespace deluge::edits
