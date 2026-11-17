/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// Compacting a Song's note_locks::Pool: drop the sets nothing refers to any more, renumbering the rest.

#include "gui/views/instrument_clip_view.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/melodic_instrument.h"
#include "model/note/note.h"
#include "model/note/note_locks.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/arpeggiator.h"
#include "scheduler_api.h"
#include <cstring>

namespace note_locks = deluge::note_locks;

namespace {

/// Calls fn(uint16_t& lockSet, context) on every pool index the Song holds outside the action log: every Note in every
/// InstrumentClip (session and arrangement-only), every ArpNote in every arpeggiator of every Output (including
/// hibernating ones), and - for the current Song - notes the clip view has lifted out of their rows. Nothing in here
/// allocates or yields.
void forEachLockSetHolder(Song& song, void (*fn)(uint16_t& lockSet, void* context), void* context) {
	for (ClipArray* clips : {&song.sessionClips, &song.arrangementOnlyClips}) {
		for (int32_t c = 0; c < clips->getNumElements(); c++) {
			Clip* clip = clips->getClipAtIndex(c);
			if (clip->type != ClipType::INSTRUMENT) {
				continue;
			}
			InstrumentClip* instrumentClip = static_cast<InstrumentClip*>(clip);
			for (int32_t r = 0; r < instrumentClip->noteRows.getNumElements(); r++) {
				NoteRow* row = instrumentClip->noteRows.getElement(r);
				for (int32_t n = 0; n < row->notes.getNumElements(); n++) {
					fn(row->notes.getElement(n)->lockSet, context);
				}
			}
		}
	}

	auto visitOutput = [&](Output* output) {
		switch (output->type) {
		case OutputType::KIT: {
			Kit* kit = static_cast<Kit*>(output);
			kit->arpeggiator.forEachLockSet(fn, context);
			for (Drum* drum = kit->firstDrum; drum != nullptr; drum = drum->next) {
				drum->arpeggiator.forEachLockSet(fn, context);
			}
			break;
		}
		case OutputType::SYNTH:
		case OutputType::MIDI_OUT:
		case OutputType::CV:
			static_cast<MelodicInstrument*>(output)->arpeggiator.forEachLockSet(fn, context);
			break;
		default:
			break;
		}
	};
	for (Output* output = song.firstOutput; output != nullptr; output = output->next) {
		visitOutput(output);
	}
	for (Output* output = song.firstHibernatingInstrument; output != nullptr; output = output->next) {
		visitOutput(output);
	}
	if (&song == currentSong) {
		instrumentClipView.forEachHeldLockSet(fn, context);
	}
}

struct RemapTable {
	uint16_t* table;
	uint32_t size;
};

} // namespace

int32_t Song::compactNoteLocks() {
	note_locks::Pool& pool = noteLocks;
	int32_t setsBefore = pool.numSets();
	if (setsBefore == 0) {
		return 0;
	}
	// Only from the main loop, between note-ons: no lock scope can be open
	if (note_locks::pendingIndex != 0 || note_locks::pending.count != 0) {
		return -1;
	}

	uint32_t limit = pool.indexLimit();
	uint16_t* remap = (uint16_t*)GeneralMemoryAllocator::get().allocLowSpeed(limit * sizeof(uint16_t));
	if (remap == nullptr) {
		return -1;
	}
	memset(remap, 0, limit * sizeof(uint16_t));
	RemapTable marks{remap, limit};

	// Mark every set still referred to
	forEachLockSetHolder(
	    *this,
	    [](uint16_t& lockSet, void* context) {
		    RemapTable& t = *static_cast<RemapTable*>(context);
		    if (lockSet != 0 && lockSet < t.size) {
			    t.table[lockSet] = 1;
		    }
	    },
	    &marks);
	int32_t referenced = 0;
	for (uint32_t i = 1; i < limit; i++) {
		referenced += (remap[i] != 0);
	}
	if (referenced == setsBefore) {
		delugeDealloc(remap);
		return 0; // Nothing to reclaim
	}

	// Build the new pool to the side. If memory runs out, nothing has been touched: keep the old one.
	note_locks::Pool fresh;
	if (!pool.buildCompacted(fresh, remap)) {
		delugeDealloc(remap);
		return -1;
	}

	// Renumber every holder and swap the pools. None of this allocates or yields, so the audio routine can't run in
	// the middle and see notes renumbered against the wrong pool.
	forEachLockSetHolder(
	    *this,
	    [](uint16_t& lockSet, void* context) {
		    RemapTable& t = *static_cast<RemapTable*>(context);
		    lockSet = lockSet < t.size ? t.table[lockSet] : 0;
	    },
	    &marks);
	pool.swap(fresh); // `fresh` now owns the old pool's memory, and frees it as it goes out of scope
	delugeDealloc(remap);
	return setsBefore - pool.numSets();
}

namespace deluge::note_locks {

bool compactionRequested = false;

void compactionTask() {
	if (!compactionRequested) [[likely]] {
		return;
	}
	// Only when no other task is suspended part-way through something (a song save holding pool views, a load...)
	if (getYieldDepth() != 0) {
		return; // Try again next time
	}
	compactionRequested = false;
	// Undo / redo history holds indices too (note array snapshots, deleted notes and clips). Only compact while it's
	// empty - so if anything was logged since it was cleared, wait for the next clear.
	if (actionLogger.firstAction[BEFORE] != nullptr || actionLogger.firstAction[AFTER] != nullptr) {
		return;
	}
	if (currentSong != nullptr) {
		currentSong->compactNoteLocks();
	}
}

} // namespace deluge::note_locks
