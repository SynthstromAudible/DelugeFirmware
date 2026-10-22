// A patch cable's strength and polarity are set by its address: a strength write makes a missing cable with the
// polarity asked (in the engine and the record) and is one undo step; a polarity change writes the cable and the
// record and is not an undo step, as legacy's polarity switch is not; a full cable set refuses a new cable; and the
// reverb recomputes after a write to the cable it copies, the sidechain's to its send.

#include "deluge-project/param_address.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/edits/set_cable_polarity.h"
#include "model/edits/set_patch_cable.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/project_bridge/patch_cable_words.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_manager.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound_instrument.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

#include <optional>

using namespace deluge::scenes;
using deluge::project::OwnerId;
using deluge::project::ParamAddress;
namespace params = deluge::modulation::params;

namespace {

ParamDescriptor just(int32_t param) {
	ParamDescriptor descriptor;
	descriptor.setToHaveParamOnly(param);
	return descriptor;
}

/// The address of @p f's synth's cable from @p source to @p destination.
ParamAddress cable(const spec::SongFixture& f, PatchSource source, int32_t destination) {
	return {.owner = OwnerId::instrument(*f.instrument->id()),
	        .space = deluge::project::ParamSpace::ENGINE,
	        .id = deluge::bridge::cable_depth(source, just(destination)),
	        .clip_id = f.clip->id()};
}

PatchCableSet& cables_of(const spec::SongFixture& f) {
	return *f.clip->paramManager.getPatchCableSet();
}

/// The engine's cable from @p source to @p destination on @p f's synth, or null.
PatchCable* engine_cable(const spec::SongFixture& f, PatchSource source, int32_t destination) {
	PatchCableSet& set = cables_of(f);
	const uint8_t index = set.getPatchCableIndex(source, just(destination), nullptr, false);
	return index == 255 ? nullptr : &set.patchCables[index];
}

/// The record's cable at @p address, or null.
const deluge::project::DocCable* recorded(const spec::SongFixture& f, const ParamAddress& address) {
	const deluge::project::DocParamManager* manager =
	    f.song->project.backed_params.find(address.owner, address.clip_id);
	if (manager == nullptr) {
		return nullptr;
	}
	for (const deluge::project::DocCable& entry : manager->cables) {
		if (entry.depth == address.id) {
			return &entry;
		}
	}
	return nullptr;
}

Error apply(const spec::SongFixture& f, deluge::edits::Edit& edit) {
	deluge::edits::Context ctx{*f.song, f.song->project};
	return deluge::edits::apply(ctx, edit);
}

Polarity other_than(Polarity polarity) {
	return polarity == Polarity::BIPOLAR ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
}

} // namespace

// clang-format off
describe cable_edits("a patch cable set by its address", ${
	it("creates a missing cable with the polarity asked, then sets its strength", _ {
		spec::SongFixture f = spec::make_synth_song();
		const PatchSource source = PatchSource::ENVELOPE_1;
		const Polarity asked = other_than(PatchCable::getDefaultPolarity(source));
		const ParamAddress address = cable(f, source, params::LOCAL_LPF_RESONANCE);
		deluge::edits::SetPatchCable edit{address, 0x12345678, asked};
		expect(apply(f, edit)).to_equal(Error::NONE);
		PatchCable* made = engine_cable(f, source, params::LOCAL_LPF_RESONANCE);
		expect(made != nullptr && made->polarity == asked && made->param.getCurrentValue() == 0x12345678).to_be_true();
		const deluge::project::DocCable* entry = recorded(f, address);
		expect(entry != nullptr && deluge::bridge::polarity_of(*entry) == asked
		       && entry->amount.current_value == 0x12345678)
		    .to_be_true();
	});

	it("sets an existing cable's strength without touching its polarity", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ParamAddress address = cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST);
		deluge::edits::SetPatchCable first{address, 0x01000000, Polarity::BIPOLAR};
		expect(apply(f, first)).to_equal(Error::NONE);
		deluge::edits::SetPatchCable second{address, 0x02000000, Polarity::UNIPOLAR};
		expect(apply(f, second)).to_equal(Error::NONE);
		PatchCable* existing = engine_cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST);
		expect(existing != nullptr && existing->polarity == Polarity::BIPOLAR
		       && existing->param.getCurrentValue() == 0x02000000)
		    .to_be_true();
	});

	it("refuses a cable on a full set, leaving the set as it was", _ {
		spec::SongFixture f = spec::make_synth_song();
		for (const PatchSource source : {PatchSource::RANDOM, PatchSource::VELOCITY, PatchSource::NOTE}) {
			for (int32_t p = 0; cables_of(f).numPatchCables < kMaxNumPatchCables && p < params::kNumParams; ++p) {
				deluge::edits::SetPatchCable fill{cable(f, source, p), 0, Polarity::UNIPOLAR};
				(void)apply(f, fill);
			}
		}
		expect(cables_of(f).numPatchCables).to_equal(static_cast<uint8_t>(kMaxNumPatchCables));
		deluge::edits::SetPatchCable one_more{cable(f, PatchSource::AFTERTOUCH, params::LOCAL_LPF_FREQ), 0x01000000,
		                                      Polarity::UNIPOLAR};
		expect(apply(f, one_more)).to_equal(Error::UNSPECIFIED);
		expect(cables_of(f).numPatchCables).to_equal(static_cast<uint8_t>(kMaxNumPatchCables));
		expect(engine_cable(f, PatchSource::AFTERTOUCH, params::LOCAL_LPF_FREQ) == nullptr).to_be_true();
	});

	it("is one undo step for a strength write, which restores the strength", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ParamAddress address = cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST);
		deluge::edits::SetPatchCable made{address, 0x01000000, Polarity::BIPOLAR};
		expect(apply(f, made)).to_equal(Error::NONE);
		changeRootUI(&sessionView); // an undo step opens only with a view as the current UI
		actionLogger.deleteAllLogs();
		deluge::edits::SetPatchCable turned{address, 0x03000000, Polarity::BIPOLAR};
		expect(apply(f, turned)).to_equal(Error::NONE);
		Action* action = actionLogger.firstAction[BEFORE];
		expect(action != nullptr && action->nextAction == nullptr).to_be_true();
		expect(actionLogger.revert(BEFORE, false, false)).to_be_true();
		expect(engine_cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST)->param.getCurrentValue())
		    .to_equal(0x01000000);
		actionLogger.deleteAllLogs();
		nullifyUIs();
	});

	it("flips an existing cable's polarity in the engine and the record, and logs no undo step", _ {
		spec::SongFixture f = spec::make_synth_song();
		const ParamAddress address = cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST);
		deluge::edits::SetPatchCable made{address, 0x01000000, Polarity::BIPOLAR};
		expect(apply(f, made)).to_equal(Error::NONE);
		changeRootUI(&sessionView);
		actionLogger.deleteAllLogs();
		deluge::edits::SetCablePolarity flip{address, Polarity::UNIPOLAR};
		expect(apply(f, flip)).to_equal(Error::NONE);
		expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
		expect(engine_cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST)->polarity == Polarity::UNIPOLAR)
		    .to_be_true();
		const deluge::project::DocCable* entry = recorded(f, address);
		expect(entry != nullptr && deluge::bridge::polarity_of(*entry) == Polarity::UNIPOLAR).to_be_true();
		nullifyUIs();
	});

	it("writes a flip to the record for a cable the record has not taken yet", _ {
		spec::SongFixture f = spec::make_synth_song();
		// A cable made in the engine alone, as the gold knob's own resolution makes one.
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* stack = setupModelStackWithThreeMainThingsIncludingNoteRow(
		    memory, f.song, f.clip, 0, nullptr, static_cast<SoundInstrument*>(f.instrument), &f.clip->paramManager);
		ParamCollectionSummary* summary = f.clip->paramManager.getPatchCableSetSummary();
		ModelStackWithParamId* with_id = stack->addParamCollectionSummary(summary)->addParamId(
		    PatchCableSet::getParamId(just(params::LOCAL_PITCH_ADJUST), PatchSource::LFO_GLOBAL_1));
		expect(summary->paramCollection->getAutoParamFromId(with_id, true)->autoParam != nullptr).to_be_true();
		const ParamAddress address = cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST);
		const Polarity flipped =
		    other_than(engine_cable(f, PatchSource::LFO_GLOBAL_1, params::LOCAL_PITCH_ADJUST)->polarity);
		deluge::edits::SetCablePolarity flip{address, flipped};
		expect(apply(f, flip)).to_equal(Error::NONE);
		const deluge::project::DocCable* entry = recorded(f, address);
		expect(entry != nullptr && deluge::bridge::polarity_of(*entry) == flipped).to_be_true();
	});

	it("refuses a polarity change on a cable that does not exist", _ {
		spec::SongFixture f = spec::make_synth_song();
		deluge::edits::SetCablePolarity flip{cable(f, PatchSource::LFO_GLOBAL_2, params::LOCAL_OSC_A_VOLUME),
		                                     Polarity::UNIPOLAR};
		expect(apply(f, flip)).to_equal(Error::UNSPECIFIED);
		expect(engine_cable(f, PatchSource::LFO_GLOBAL_2, params::LOCAL_OSC_A_VOLUME) == nullptr).to_be_true();
	});

	it("raises the reverb's recompute after a write to the sidechain-to-reverb cable", _ {
		spec::SongFixture f = spec::make_synth_song();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
		deluge::edits::SetPatchCable send{cable(f, PatchSource::SIDECHAIN, params::GLOBAL_VOLUME_POST_REVERB_SEND),
		                                  0x01000000, Polarity::UNIPOLAR};
		expect(apply(f, send)).to_equal(Error::NONE);
		expect(AudioEngine::mustUpdateReverbParamsBeforeNextRender).to_be_true();
		AudioEngine::mustUpdateReverbParamsBeforeNextRender = false;
	});
});

CPPSPEC_SPEC(cable_edits)
