#include "deluge-project/param_address.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/song.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::engine::ParamId;

namespace {
DocParamSet block(uint32_t first, size_t count) {
	DocParamSet set;
	set.first = ParamId{first};
	set.params.resize(count);
	return set;
}
} // namespace

// clang-format off
describe param_store("the model's param stores, addressed by id", ${
	it("finds a lane by id from the store's first id", _ {
		DocParamSet set = block(90, 43);
		expect(set.find(ParamId{90}) == &set.params[0]).to_equal(true);
		expect(set.find(ParamId{132}) == &set.params[42]).to_equal(true);
		expect(set.find(ParamId{89}) == nullptr).to_equal(true);
		expect(set.find(ParamId{133}) == nullptr).to_equal(true);
		expect(set.indexOf(ParamId{101}).value()).to_equal(size_t{11});
	});

	it("keeps an engine's two blocks apart", _ {
		DocParamManager mgr;
		mgr.engine_params[0] = block(0, 55);
		mgr.engine_params[1] = block(90, 27);
		expect(mgr.findEngineParam(ParamId{23}) == &mgr.engine_params[0]->params[23]).to_equal(true);
		expect(mgr.findEngineParam(ParamId{101}) == &mgr.engine_params[1]->params[11]).to_equal(true);
		expect(mgr.findEngineParam(ParamId{60}) == nullptr).to_equal(true);
	});

	it("resolves a song's own param by its id", _ {
		Song song;
		song.master_params.engine_params[1] = block(90, 43);
		ParamAddress volume{OwnerId::song(), ParamSpace::ENGINE, ParamId{129}};
		expect(song.resolveParam(volume) == &song.master_params.engine_params[1]->params[39]).to_equal(true);
		ParamAddress outside{OwnerId::song(), ParamSpace::ENGINE, ParamId{39}};
		expect(song.resolveParam(outside) == nullptr).to_equal(true);
	});

	it("resolves an instrument's lane per clip and a drum's apart from it", _ {
		Song song;
		song.backed_params.getOrCreate(OwnerId::instrument(7), 3).engine_params[1] = block(90, 27);
		song.backed_params.getOrCreate(OwnerId::drum(7), 3).engine_params[1] = block(90, 27);
		ParamAddress gate{OwnerId::instrument(7), ParamSpace::ENGINE, ParamId{101}, 3};
		DocAutoParam* lane = song.resolveParam(gate);
		expect(lane == song.backed_params.find(OwnerId::instrument(7), 3)->findEngineParam(ParamId{101}))
		    .to_equal(true);
		ParamAddress drumGate{OwnerId::drum(7), ParamSpace::ENGINE, ParamId{101}, 3};
		expect(song.resolveParam(drumGate) != lane).to_equal(true);
		ParamAddress otherClip{OwnerId::instrument(7), ParamSpace::ENGINE, ParamId{101}, 4};
		expect(song.resolveParam(otherClip) == nullptr).to_equal(true);
	});

	it("resolves the host spaces: expression, a MIDI CC and a cable", _ {
		Song song;
		DocParamManager& clip = song.clip_params.getOrCreate(5);
		clip.expression = block(0, 3);
		clip.midi_params.push_back(DocMidiParam{.cc = 74});
		DocParamManager& synth = song.backed_params.getOrCreate(OwnerId::instrument(9), std::nullopt);
		synth.patch_cables.push_back(DocPatchCable{.from = 4, .destination = 0xFFFFFF02u});
		expect(song.resolveParam({OwnerId::clip(5), ParamSpace::EXPRESSION, ParamId{2}})
		       == &clip.expression->params[2]).to_equal(true);
		expect(song.resolveParam({OwnerId::clip(5), ParamSpace::MIDI_CC, ParamId{74}})
		       == &clip.midi_params[0].param).to_equal(true);
		ParamAddress cable{OwnerId::instrument(9), ParamSpace::PATCH_CABLE,
		                   ParamId{patch_cable_param_id(0xFFFFFF02u, 4)}};
		expect(song.resolveParam(cable) == &synth.patch_cables[0].amount).to_equal(true);
	});
});

CPPSPEC_SPEC(param_store)
