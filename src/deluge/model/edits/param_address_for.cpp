#include "model/edits/param_address_for.h"

#include "definitions_cxx.hpp" // ALPHA_OR_BETA_VERSION
#include "foundation/panic.h"  // FREEZE_WITH_ERROR
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/params/param_collection.h"

namespace deluge::edits {

// The owner classification of ModelStackWithAutoParam::doc(), extracted verbatim (behaviour-identical) so an Edit
// can build the same stable ParamAddress at construction time, before it holds a resolved engine ModelStack. The
// `kind` carried into the address is the ModelStack's threaded param Kind (S5-a); the ALPHA safety-net assert that
// it matches the collection's own getParamKind() rides along with it, unchanged. The one unaddressable case — a
// never-saved (no-serialId) owner — returns nullopt, exactly the fallthrough doc() takes to its engine store.
std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack) {
	if (modelStack.song == nullptr) {
		return std::nullopt;
	}
	TimelineCounter* tc = modelStack.getTimelineCounterAllowNull();
	deluge::project::OwnerId owner{};
	std::optional<uint32_t> clipSerial{}; // for INSTRUMENT/DRUM owners: which clip's override this lane is
	bool haveOwner = false;
	if (tc == static_cast<TimelineCounter*>(modelStack.song)) {
		owner = deluge::project::OwnerId::song();
		haveOwner = true;
	}
	else if (tc != nullptr) {
		// Song and Clip are the only TimelineCounters, so a non-song tc is a Clip. Classify the owner by the
		// ModelStack context (S3c-S3g): a set noteRow ⇒ a per-drum param (OwnerId::drum, keyed by the drum's
		// serialId); else a synth/kit InstrumentClip ⇒ clip-level instrument-shared (OwnerId::instrument(output));
		// else an AudioClip ⇒ its own params (OwnerId::clip). noteRow is a RELIABLE drum signal — the ModelStack
		// builders always set it explicitly (addNoteRow(0, nullptr) for the no-noteRow path), never stale. Any
		// uncertain case (non-Sound drum, missing serialId) leaves haveOwner false → the engine fallback (nullopt).
		Clip* clip = static_cast<Clip*>(tc);
		if (std::optional<uint32_t> clipId = clip->serialId()) {
			NoteRow* noteRow = modelStack.getNoteRowAllowNull();
			if (noteRow != nullptr) {
				if (noteRow->drum != nullptr && noteRow->drum->type == DrumType::SOUND) {
					if (std::optional<uint32_t> drumId = noteRow->drum->serialId()) {
						owner = deluge::project::OwnerId::drum(*drumId);
						clipSerial = clipId;
						haveOwner = true;
					}
				}
			}
			else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
			         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)) {
				if (std::optional<uint32_t> outId = clip->output->serialId()) {
					owner = deluge::project::OwnerId::instrument(*outId);
					clipSerial = clipId;
					haveOwner = true;
				}
			}
			else {
				owner = deluge::project::OwnerId::clip(*clipId);
				haveOwner = true;
			}
		}
		else if (clip->type == ClipType::AUDIO) {
			// S5-b.1: a never-saved (no-serialId) audio clip is addressed by its transient session id
			// (session_clip_params), so doc() id-resolves it instead of hitting the paramCollection fallback. The
			// session id is minted + the store relocated in the post-load pass; 0 means not-yet-relocated (fallback).
			if (uint32_t sid = clip->sessionId(); sid != 0) {
				owner = deluge::project::OwnerId::clipSession(sid);
				haveOwner = true;
			}
		}
	}
	if (!haveOwner) {
		return std::nullopt;
	}
#if ALPHA_OR_BETA_VERSION
	// Safety net for the threaded `kind`: every ModelStack builder that sets paramCollection must set kind to
	// match. A mismatch means a builder (or the oracle stub) missed it — catch it loudly before it mis-resolves a
	// ParamAddress against the wrong tree entry.
	if (modelStack.kind != modelStack.paramCollection->getParamKind()) {
		FREEZE_WITH_ERROR("DkND");
	}
#endif
	return deluge::project::ParamAddress{owner, modelStack.kind, modelStack.paramId, clipSerial};
}

} // namespace deluge::edits
