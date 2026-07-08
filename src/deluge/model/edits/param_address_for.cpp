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
	bool clipDimIsSession = false;        // for INSTRUMENT/DRUM owners: is clipSerial a session id (mixed case)?
	bool haveOwner = false;
	if (tc == static_cast<TimelineCounter*>(modelStack.song)) {
		owner = deluge::project::OwnerId::song();
		haveOwner = true;
	}
	else if (tc != nullptr) {
		// Song and Clip are the only TimelineCounters, so a non-song tc is a Clip. Classify the owner by the
		// ModelStack context (S3c-S3g): a set noteRow ⇒ a per-drum param (OwnerId::drum, keyed by the drum);
		// else a synth/kit InstrumentClip ⇒ clip-level instrument-shared (OwnerId::instrument(output)); else an
		// AudioClip / MIDI clip ⇒ its own params (OwnerId::clip). noteRow is a RELIABLE drum signal — the ModelStack
		// builders always set it explicitly (addNoteRow(0, nullptr) for the no-noteRow path), never stale.
		//
		// S5-b: the OWNER dimension (clip / output / drum) and the CLIP dimension are each addressed by a persisted
		// serialId when the object was saved, else by a transient session id (id-address a not-yet-saved owner so
		// doc() resolves it instead of the paramCollection fallback). The two dimensions are INDEPENDENT — a saved
		// instrument (serialId) can carry a runtime-added clip (session id): the "mixed" case, disambiguated by
		// clip_is_session. A missing id in EITHER dimension (never relocated) leaves haveOwner false → the fallback.
		Clip* clip = static_cast<Clip*>(tc);
		NoteRow* noteRow = modelStack.getNoteRowAllowNull();

		// Clip dimension: serialId (persisted) or session id (runtime / never-saved). Used as the (owner, clip) key
		// for INSTRUMENT/DRUM owners, and as the CLIP owner's own id for audio/MIDI.
		std::optional<uint32_t> clipSerialOpt = clip->serialId();
		bool haveClip = false;
		uint32_t clipKey = 0;
		bool clipIsSession = false;
		if (clipSerialOpt.has_value()) {
			clipKey = *clipSerialOpt;
			haveClip = true;
		}
		else if (clip->sessionId() != 0) {
			clipKey = clip->sessionId();
			clipIsSession = true;
			haveClip = true;
		}

		if (!haveClip) {
			// clip not relocated → fallback (haveOwner stays false)
		}
		else if (noteRow != nullptr) {
			// Per-drum (DRUM owner): resolve the drum dimension independently of the clip dimension.
			if (noteRow->drum != nullptr && noteRow->drum->type == DrumType::SOUND) {
				Drum* drum = noteRow->drum;
				if (std::optional<uint32_t> drumId = drum->serialId()) {
					owner = deluge::project::OwnerId::drum(*drumId);
					clipSerial = clipKey;
					haveOwner = true;
				}
				else if (drum->sessionId() != 0) {
					owner = deluge::project::OwnerId::drumSession(drum->sessionId());
					clipSerial = clipKey;
					haveOwner = true;
				}
			}
		}
		else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
		         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)) {
			// Synth/kit clip-level instrument-shared (INSTRUMENT owner): resolve the output dimension independently.
			Output* out = clip->output;
			if (std::optional<uint32_t> outId = out->serialId()) {
				owner = deluge::project::OwnerId::instrument(*outId);
				clipSerial = clipKey;
				haveOwner = true;
			}
			else if (out->sessionId() != 0) {
				owner = deluge::project::OwnerId::instrumentSession(out->sessionId());
				clipSerial = clipKey;
				haveOwner = true;
			}
		}
		else if (clip->type == ClipType::AUDIO
		         || (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
		             && clip->output->type == OutputType::MIDI_OUT)) {
			// An audio clip's own params, or a MIDI_OUT clip's per-clip CC automation (Kind::MIDI resolves by cc
			// within the per-clip store) — both are CLIP owners keyed by the clip's serialId or session id.
			owner = clipIsSession ? deluge::project::OwnerId::clipSession(clipKey)
			                      : deluge::project::OwnerId::clip(clipKey);
			haveOwner = true;
		}
		if (haveOwner) {
			// For INSTRUMENT/DRUM owners, record whether the clip dimension is a session id (the mixed case's
			// disambiguator). Harmless for CLIP owners (clip_serial unused there; is_session rides in the OwnerId).
			clipDimIsSession = clipIsSession;
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
	return deluge::project::ParamAddress{owner, modelStack.kind, modelStack.paramId, clipSerial, clipDimIsSession};
}

} // namespace deluge::edits
