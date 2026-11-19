#include "effects/host_chain/params.h"
#include "instruments/deluge/params.h"

#include <cppspec.hpp>

using namespace deluge::instrument;
using deluge::plugin::ParamId;
using deluge::plugin::ParamKind;

// clang-format off
describe param_predicates("which collection and id a param is, and what the knob does with it", ${
	it("numbers a collection's index as the runtime does", _ {
		expect(runtime_param_id(ParamKind::PATCHED, 2).value).to_equal(2U);
		expect(runtime_param_id(ParamKind::UNPATCHED_SOUND, 12).value).to_equal(102U);
		expect(runtime_param_id(ParamKind::UNPATCHED_GLOBAL, 0).value).to_equal(90U);
		expect(runtime_param_id(ParamKind::NONE, 3).value).to_equal(93U);
		expect(runtime_param_id(ParamKind::EXPRESSION, 1).value).to_equal(1U);
		expect(param_row(ParamKind::PATCHED, runtime_param_id(ParamKind::PATCHED, -1))).to_be_null();
	});

	it("finds the row of a sound's patched or unpatched param", _ {
		expect(param_row(ParamKind::PATCHED, param::kVolume)->id.value).to_equal(param::kVolume.value);
		expect(param_row(ParamKind::UNPATCHED_SOUND, param::kRhythm)->id.value).to_equal(param::kRhythm.value);
	});

	it("finds a global effectable's rows in the host chain's table and no patched row there", _ {
		expect(param_row(ParamKind::UNPATCHED_GLOBAL, deluge::host_chain::param::kTempo)->id.value)
		    .to_equal(deluge::host_chain::param::kTempo.value);
		expect(param_row(ParamKind::UNPATCHED_GLOBAL, param::kVolume)).to_be_null();
		expect(param_row(ParamKind::UNPATCHED_SOUND, param::kVolume)).to_be_null();
		expect(param_row(ParamKind::EXPRESSION, ParamId{0})).to_be_null();
	});

	it("knows the arp rhythm only as a sound's unpatched param", _ {
		expect(is_arp_rhythm(ParamKind::UNPATCHED_SOUND, param::kRhythm)).to_be_true();
		expect(is_arp_rhythm(ParamKind::UNPATCHED_GLOBAL, param::kRhythm)).to_be_false();
		expect(is_arp_rhythm(ParamKind::UNPATCHED_SOUND, param::kStutterRate)).to_be_false();
	});

	it("knows the stutter rate under both unpatched kinds", _ {
		expect(is_stutter_rate(ParamKind::UNPATCHED_GLOBAL, deluge::host_chain::param::kStutterRate)).to_be_true();
		expect(is_stutter_rate(ParamKind::UNPATCHED_SOUND, deluge::host_chain::param::kStutterRate)).to_be_true();
		expect(is_stutter_rate(ParamKind::PATCHED, deluge::host_chain::param::kStutterRate)).to_be_false();
		expect(is_stutter_rate(ParamKind::UNPATCHED_SOUND, param::kRhythm)).to_be_false();
	});

	it("calls a centred param, a cable depth and the pitch-bend axis bipolar", _ {
		expect(is_bipolar(ParamKind::PATCHED, param::kPan)).to_be_true();
		expect(is_bipolar(ParamKind::UNPATCHED_GLOBAL, deluge::host_chain::param::kPan)).to_be_true();
		expect(is_bipolar(ParamKind::PATCH_CABLE, ParamId{0})).to_be_true();
		expect(is_bipolar(ParamKind::EXPRESSION, kExpressionPitchBend)).to_be_true();
	});

	it("calls an uncentred param, another expression axis and a host space bipolar-free", _ {
		expect(is_bipolar(ParamKind::PATCHED, param::kVolume)).to_be_false();
		expect(is_bipolar(ParamKind::UNPATCHED_SOUND, param::kRhythm)).to_be_false();
		expect(is_bipolar(ParamKind::EXPRESSION, ParamId{1})).to_be_false();
		expect(is_bipolar(ParamKind::MIDI, ParamId{0})).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(param_predicates)
