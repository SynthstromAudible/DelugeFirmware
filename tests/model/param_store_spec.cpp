#include "deluge-project/param_address.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/song.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::plugin::ParamId;

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
		expect(set.find(ParamId{90})).to_equal(&set.params[0]);
		expect(set.find(ParamId{132})).to_equal(&set.params[42]);
		expect(set.find(ParamId{89})).to_be_null();
		expect(set.find(ParamId{133})).to_be_null();
		expect(set.indexOf(ParamId{101}).value()).to_equal(size_t{11});
	});

	it("keeps an engine's two blocks apart", _ {
		DocParamManager mgr;
		mgr.engine_params[0] = block(0, 55);
		mgr.engine_params[1] = block(90, 27);
		expect(mgr.findEngineParam(ParamId{23})).to_equal(&mgr.engine_params[0]->params[23]);
		expect(mgr.findEngineParam(ParamId{101})).to_equal(&mgr.engine_params[1]->params[11]);
		expect(mgr.findEngineParam(ParamId{60})).to_be_null();
	});

	it("resolves a song's own param by its id", _ {
		Song song;
		song.master_params.engine_params[1] = block(90, 43);
		ParamAddress volume{OwnerId::song(), ParamSpace::ENGINE, ParamId{129}};
		expect(song.resolveParam(volume)).to_equal(&song.master_params.engine_params[1]->params[39]);
		ParamAddress outside{OwnerId::song(), ParamSpace::ENGINE, ParamId{39}};
		expect(song.resolveParam(outside)).to_be_null();
	});

	it("resolves an instrument's lane per clip and a drum's apart from it", _ {
		Song song;
		song.backed_params.getOrCreate(OwnerId::instrument(7), 3).engine_params[1] = block(90, 27);
		song.backed_params.getOrCreate(OwnerId::drum(7), 3).engine_params[1] = block(90, 27);
		ParamAddress gate{OwnerId::instrument(7), ParamSpace::ENGINE, ParamId{101}, 3};
		DocAutoParam* lane = song.resolveParam(gate);
		expect(lane).to_equal(song.backed_params.find(OwnerId::instrument(7), 3)->findEngineParam(ParamId{101}));
		ParamAddress drumGate{OwnerId::drum(7), ParamSpace::ENGINE, ParamId{101}, 3};
		expect(song.resolveParam(drumGate)).not_().to_equal(lane);
		ParamAddress otherClip{OwnerId::instrument(7), ParamSpace::ENGINE, ParamId{101}, 4};
		expect(song.resolveParam(otherClip)).to_be_null();
	});

	it("resolves the host spaces: expression and a MIDI CC", _ {
		Song song;
		DocParamManager& clip = song.clip_params.getOrCreate(5);
		clip.expression = block(0, 3);
		clip.midi_params.push_back(DocMidiParam{.cc = 74});
		expect(song.resolveParam({OwnerId::clip(5), ParamSpace::EXPRESSION, ParamId{2}})).to_equal(&clip.expression->params[2]);
		expect(song.resolveParam({OwnerId::clip(5), ParamSpace::MIDI_CC, ParamId{74}})).to_equal(&clip.midi_params[0].param);
	});

	it("resolves a cable's depth as an engine param, per clip", _ {
		Song song;
		// velocity (12) -> volume (2): the instrument's depth id 0x4000'0d02
		constexpr ParamId depth{0x4000'0d02u};
		DocParamManager& shared = song.backed_params.getOrCreate(OwnerId::instrument(9), std::nullopt);
		shared.cables.push_back(DocCable{.depth = depth, .polarity = 1});
		DocParamManager& clip = song.backed_params.getOrCreate(OwnerId::instrument(9), 3);
		clip.cables.push_back(DocCable{.depth = depth});
		expect(song.resolveParam({OwnerId::instrument(9), ParamSpace::ENGINE, depth})).to_equal(&shared.cables[0].amount);
		expect(song.resolveParam({OwnerId::instrument(9), ParamSpace::ENGINE, depth, 3})).to_equal(&clip.cables[0].amount);
		expect(song.resolveParam({OwnerId::instrument(9), ParamSpace::ENGINE, ParamId{0x4000'0e02u}})).to_be_null();
	});
});

CPPSPEC_SPEC(param_store)
