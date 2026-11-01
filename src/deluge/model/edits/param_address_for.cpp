#include "model/edits/param_address_for.h"

#include "definitions_cxx.hpp" // ALPHA_OR_BETA_VERSION
#include "deluge-project/edit_guard.h"
#include "foundation/panic.h" // FREEZE_WITH_ERROR
#include "memory/heaps.h"     // onRenderTier
#include "model/clip/clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/project_bridge/param_ids.h"
#include "model/project_bridge/param_projection.h"
#include "model/song/song.h"
#include "modulation/midi/midi_param_collection.h"
#include "modulation/params/param_collection.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"

namespace deluge::edits {

#if ALPHA_OR_BETA_VERSION
uint32_t docFallbackHits = 0;
#endif

namespace {
/// The output or sound drum whose own param manager @p modControllable is, as an owner with no clip: an arranger row's
/// backed-up params, named by the stack's mod controllable alone.
std::optional<deluge::project::ParamAddress> backedOwnerOf(::Song& song, ModControllable* modControllable) {
	if (modControllable == nullptr) {
		return std::nullopt;
	}
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->toModControllable() == modControllable) {
			const std::optional<uint32_t> id = output->id();
			return id ? std::optional{deluge::project::ParamAddress{.owner = deluge::project::OwnerId::instrument(*id)}}
			          : std::nullopt;
		}
		if (output->type != OutputType::KIT) {
			continue;
		}
		for (Drum* drum = static_cast<Kit*>(output)->firstDrum; drum != nullptr; drum = drum->next) {
			if (drum->toModControllable() == modControllable) {
				const std::optional<uint32_t> id = drum->id();
				return id ? std::optional{deluge::project::ParamAddress{.owner = deluge::project::OwnerId::drum(*id)}}
				          : std::nullopt;
			}
		}
	}
	return std::nullopt;
}
} // namespace

std::optional<deluge::project::ParamAddress> ownerAddressFor(const ModelStackWithThreeMainThings& modelStack) {
	if (modelStack.song == nullptr) {
		return std::nullopt;
	}
	TimelineCounter* tc = modelStack.getTimelineCounterAllowNull();
	if (tc == nullptr) {
		// The render plays no backed-up params, and must not walk the output lists the interaction tier edits.
		return deluge::memory::onRenderTier() ? std::nullopt
		                                      : backedOwnerOf(*modelStack.song, modelStack.modControllable);
	}
	if (tc == static_cast<TimelineCounter*>(modelStack.song)) {
		return deluge::project::ParamAddress{.owner = deluge::project::OwnerId::song()};
	}
	// Song and Clip are the only TimelineCounters, so a non-song tc is a Clip. A set noteRow is a kit row's drum
	// (OwnerId::drum, any drum type) or a melodic clip's note row (OwnerId::note_row); else a synth or kit clip's own
	// params are its instrument's (OwnerId::instrument(output)); else an audio, MIDI or CV clip owns its params
	// (OwnerId::clip). The builders always set noteRow explicitly (addNoteRow(0, nullptr) for no row), never stale.
	// Each dimension (clip, output, drum) is addressed by its durable id; a missing id leaves no address.
	Clip* clip = static_cast<Clip*>(tc);
	const std::optional<uint32_t> clipId = clip->id();
	if (!clipId) {
		return std::nullopt;
	}
	if (NoteRow* noteRow = modelStack.getNoteRowAllowNull()) {
		if (noteRow->drum == nullptr) {
			// A melodic clip's note row: its MPE lanes, named by the clip and the row's note.
			return deluge::project::ParamAddress{.owner =
			                                         deluge::project::OwnerId::note_row(*clipId, modelStack.noteRowId)};
		}
		const std::optional<uint32_t> drumId = noteRow->drum->id();
		return drumId ? std::optional{deluge::project::ParamAddress{.owner = deluge::project::OwnerId::drum(*drumId),
		                                                            .clip_id = clipId}}
		              : std::nullopt;
	}
	if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
	    && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)) {
		const std::optional<uint32_t> outId = clip->output->id();
		return outId ? std::optional{deluge::project::ParamAddress{
		                   .owner = deluge::project::OwnerId::instrument(*outId), .clip_id = clipId}}
		             : std::nullopt;
	}
	if (clip->type == ClipType::AUDIO
	    || (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
	        && (clip->output->type == OutputType::MIDI_OUT || clip->output->type == OutputType::CV))) {
		// An audio clip's own params, a MIDI clip's CC automation, a MIDI or CV clip's expression: CLIP owners keyed
		// by the clip's durable id.
		return deluge::project::ParamAddress{.owner = deluge::project::OwnerId::clip(*clipId)};
	}
	return std::nullopt;
}

// The owner classification of ModelStackWithAutoParam::doc(), so an Edit or an undo record can name the lane by a
// stable ParamAddress. The `kind` carried into the address is the ModelStack's threaded param Kind; an ALPHA
// safety-net assert checks it matches the collection's own getParamKind().
std::optional<deluge::project::ParamAddress> addressFor(const ModelStackWithAutoParam& modelStack) {
	const std::optional<deluge::project::ParamAddress> owner = ownerAddressFor(modelStack);
	if (!owner) {
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
	return deluge::bridge::addressOf(owner->owner, modelStack.kind, modelStack.paramId, owner->clip_id);
}

namespace {
// The DocParamManager for `address` in the project tree, creating an empty entry if absent. Only the CLIP /
// INSTRUMENT / DRUM owner kinds reach here (SONG is always resolvable, so relocateAndResolve returns early for it).
deluge::project::DocParamManager* managerForAddress(deluge::project::Song& proj,
                                                    const deluge::project::ParamAddress& address) {
	switch (address.owner.kind) {
	case deluge::project::OwnerId::Kind::CLIP:
		return &proj.clip_params.getOrCreate(address.owner.id);
	case deluge::project::OwnerId::Kind::INSTRUMENT:
	case deluge::project::OwnerId::Kind::DRUM:
		return &proj.backed_params.getOrCreate(address.owner, address.clip_id);
	case deluge::project::OwnerId::Kind::SONG:
	case deluge::project::OwnerId::Kind::NOTE_ROW:
		return nullptr;
	}
	return nullptr;
}
// The DocParamManager for `address` if the project tree holds one; creates nothing.
deluge::project::DocParamManager* existingManagerForAddress(deluge::project::Song& proj,
                                                            const deluge::project::ParamAddress& address) {
	switch (address.owner.kind) {
	case deluge::project::OwnerId::Kind::CLIP:
		return proj.clip_params.find(address.owner.id);
	case deluge::project::OwnerId::Kind::INSTRUMENT:
	case deluge::project::OwnerId::Kind::DRUM:
		return proj.backed_params.find(address.owner, address.clip_id);
	case deluge::project::OwnerId::Kind::SONG:
	case deluge::project::OwnerId::Kind::NOTE_ROW:
		return nullptr;
	}
	return nullptr;
}
} // namespace

deluge::project::DocAutoParam* relocateAndResolve(const ModelStackWithAutoParam& modelStack) {
	if (modelStack.song == nullptr) {
		return nullptr;
	}
	TimelineCounter* tc = modelStack.getTimelineCounterAllowNull();
	// A runtime owner is always a Clip (or its NoteRow's drum). The song-global tier is bound at load and never
	// reaches here; a null tc has no owner to relocate.
	if (tc == nullptr || tc == static_cast<TimelineCounter*>(modelStack.song)) {
		return nullptr;
	}
	deluge::project::Song& proj = modelStack.song->project;
	Clip* clip = static_cast<Clip*>(tc);
	NoteRow* noteRow = modelStack.getNoteRowAllowNull();

	// Ensure the owner chain this param belongs to has a durable id, minting one for a runtime owner never
	// saved/loaded (clips always have one from construction). After this, addressFor can build a total ParamAddress.
	// The render reads the id to address the owner, so only its write is in a section.
	if (noteRow != nullptr) {
		if (noteRow->drum != nullptr && !noteRow->drum->id().has_value()) {
			const uint32_t id = modelStack.song->generateNewDrumId();
			deluge::project::EditGuard guard{deluge::project::ModelEdit::PARAM_STORE};
			noteRow->drum->setId(id);
		}
	}
	else if (clip->type == ClipType::INSTRUMENT && clip->output != nullptr
	         && (clip->output->type == OutputType::SYNTH || clip->output->type == OutputType::KIT)
	         && !clip->output->id().has_value()) {
		const uint32_t id = modelStack.song->generateNewOutputId();
		deluge::project::EditGuard guard{deluge::project::ModelEdit::PARAM_STORE};
		clip->output->setId(id);
	}

	std::optional<deluge::project::ParamAddress> address = addressFor(modelStack);
	if (!address.has_value() || address->owner.kind == deluge::project::OwnerId::Kind::NOTE_ROW) {
		return nullptr; // no tree slot: a melodic note row's lanes stay in its own expression set
	}

	// Relocate the TOUCHED collection's engine store into its tree slot, as load does. Only
	// this collection moves; the owner's other collections relocate on their own first touch (each is an independent
	// slot / seed of the same (owner, clip) DocParamManager). Guarded by the slot being empty (a fresh owner just
	// created by getOrCreate) so an already-relocated collection is never double-moved.
	//
	// Whatever allocates or copies is done first, outside any section: the CC lanes' copy, and an engine set's own
	// store. The section holds only what the render resolves through: the store map's insert, then the move or
	// assignment into the slot and the rebind, so it sees the owner wholly before or wholly after. The render itself
	// never relocates (ModelStackWithAutoParam::doc()).
	ParamCollection* pc = modelStack.paramCollection;
	const bool isCable = modelStack.kind == deluge::modulation::params::Kind::PATCH_CABLE;
	const bool isMidi = !isCable && address->space == deluge::project::ParamSpace::MIDI_CC;
	auto* midi = isMidi ? static_cast<MIDIParamCollection*>(pc) : nullptr;
	auto* set = (!isCable && !isMidi) ? static_cast<ParamSet*>(pc) : nullptr;
	std::vector<deluge::project::DocMidiParam> midiLanes;
	if (midi != nullptr && midi->domMidi() == nullptr) {
		midiLanes = deluge::bridge::toDocMidiParams(*midi);
	}
	if (set != nullptr) {
		deluge::project::DocParamManager* existing = existingManagerForAddress(proj, *address);
		if (existing == nullptr || !deluge::bridge::storeFor(*existing, set->getParamKind())->has_value()) {
			set->bindDomStore(); // ensure an own store exists to move (idempotent if already bound)
		}
	}

	deluge::project::EditGuard guard{deluge::project::ModelEdit::PARAM_STORE};
	deluge::project::DocParamManager* mgr = managerForAddress(proj, *address);
	if (mgr == nullptr) {
		return nullptr;
	}
	if (isCable) {
		auto* pcs = static_cast<PatchCableSet*>(pc);
		if (!pcs->depthsRelocated()) {
			pcs->relocateDepthsInto(mgr->cable_depths);
		}
	}
	else if (midi != nullptr) {
		if (midi->domMidi() == nullptr) {
			mgr->midi_params = std::move(midiLanes);
			midi->bindDomMidiExternal(&mgr->midi_params);
		}
	}
	else {
		std::optional<deluge::project::DocParamSet>& slot = *deluge::bridge::storeFor(*mgr, set->getParamKind());
		if (!slot.has_value()) {
			slot = std::move(*set->domStore()); // move the engine set's own store into the tree slot
			set->bindDomStoreExternal(&*slot);  // rebind the engine set at the tree copy
		}
	}
	return proj.resolveParam(*address);
}

} // namespace deluge::edits
