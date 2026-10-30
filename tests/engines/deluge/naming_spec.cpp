#include "engines/deluge/cables.h"
#include "engines/deluge/naming.h"
#include "engines/deluge/params.h"

#include <cppspec.hpp>

using namespace deluge::instrument;
using deluge::plugin::ParamId;
using deluge::plugin::ParamRef;

namespace {
uint32_t depth(PatchSource source, ParamId destination) {
	return cable_depth_id(source, destination)->value;
}
} // namespace

// clang-format off
describe naming("the Deluge instrument's names for its params", ${
	it("names a param by its file name", _ {
		expect(kNaming.id_of({"lpfFrequency"})->value).to_equal(param::kLpfFrequency.value);
		expect(kNaming.ref_of(param::kLpfFrequency).param).to_equal(std::string_view("lpfFrequency"));
	});

	it("names a cable's depth by its param and sources, nearest the param first", _ {
		const uint32_t pitchDepth = depth(PatchSource::LFO_GLOBAL_1, param::kPitch);
		expect(kNaming.id_of({"pitch", "lfo1"})->value).to_equal(pitchDepth);
		const uint32_t nested = depth(PatchSource::ENVELOPE_1, ParamId{pitchDepth});
		expect(kNaming.id_of({"pitch", "lfo1", "envelope2"})->value).to_equal(nested);
		const ParamRef ref = kNaming.ref_of(ParamId{nested});
		expect(ref.param).to_equal(std::string_view("pitch"));
		expect(ref.source).to_equal(std::string_view("lfo1"));
		expect(ref.second_source).to_equal(std::string_view("envelope2"));
	});

	it("names nothing for an unknown param or the pre-3.2 range", _ {
		expect(kNaming.id_of({"nosuchparam"})).not_().to_have_value();
		expect(kNaming.id_of({"range"})).not_().to_have_value();
	});

	it("points a mod knob at the volume the runtime uses", _ {
		expect(kNaming.mod_knob_target(param::kVolume).value).to_equal(param::kVolumePostFX.value);
		expect(kNaming.mod_knob_target(param::kVolumePostReverbSend).value).to_equal(param::kVolumePostFX.value);
		expect(kNaming.mod_knob_target(ParamId{depth(PatchSource::SIDECHAIN, param::kVolumePostFX)}).value)
		    .to_equal(depth(PatchSource::SIDECHAIN, param::kVolumePostReverbSend));
		expect(kNaming.mod_knob_target(ParamId{depth(PatchSource::LFO_GLOBAL_1, param::kVolumePostFX)}).value)
		    .to_equal(depth(PatchSource::LFO_GLOBAL_1, param::kVolume));
		expect(kNaming.mod_knob_target(param::kPan).value).to_equal(param::kPan.value);
	});
});

CPPSPEC_SPEC(naming)
