#include "host/chains/params.h"
#include "instruments/classic/params.h"

#include <cppspec.hpp>

using namespace deluge::instruments::classic;
using deluge::plugin::ParamId;
using deluge::plugin::ParamKind;

// clang-format off
describe param_predicates("which collection and id a param is, and what the knob does with it", ${
	it("numbers a collection's index as the runtime does", _ {
		expect(runtime_param_id(ParamKind::PATCHED, 2)->value).to_equal(2U);
		expect(runtime_param_id(ParamKind::UNPATCHED_SOUND, 12)->value).to_equal(102U);
		expect(runtime_param_id(ParamKind::UNPATCHED_GLOBAL, 0)->value).to_equal(90U);
		expect(runtime_param_id(ParamKind::NONE, 3)->value).to_equal(93U);
		expect(runtime_param_id(ParamKind::EXPRESSION, 1)->value).to_equal(1U);
	});

	it("gives a patch cable's packed word and an out-of-range index no id", _ {
		expect(runtime_param_id(ParamKind::PATCH_CABLE, 0x0102U)).not_().to_have_value();
		expect(runtime_param_id(ParamKind::UNPATCHED_SOUND, 0xFFFFFFFFU)).not_().to_have_value();
		expect(param_row_at(ParamKind::PATCHED, 0xFFFFFFFFU)).to_be_null();
	});

	it("names a constant id's runtime index", _ {
		expect(static_cast<uint32_t>(runtime_index(param::kPan))).to_equal(23U);
		expect(static_cast<uint32_t>(runtime_index(param::kPortamento))).to_equal(11U);
		expect(static_cast<uint32_t>(runtime_index(kFirstGlobalParam))).to_equal(48U);
		expect(static_cast<uint32_t>(runtime_index(param::kDx7Level))).to_equal(45U);
	});

	it("finds a row by index as it does by id", _ {
		expect(param_row_at(ParamKind::PATCHED, 2)).to_equal(param_row(ParamKind::PATCHED, param::kVolume));
		expect(param_row_at(ParamKind::UNPATCHED_SOUND, 11))
		    .to_equal(param_row(ParamKind::UNPATCHED_SOUND, param::kPortamento));
		expect(param_row_at(ParamKind::UNPATCHED_GLOBAL, 26)).to_equal(
		    param_row(ParamKind::UNPATCHED_GLOBAL, deluge::host::chains::param::kTempo));
		expect(param_row_at(ParamKind::UNPATCHED_SOUND, 12)).to_be_null();
		expect(param_row_at(ParamKind::EXPRESSION, 0)).to_be_null();
	});

	it("answers the predicates by index too", _ {
		expect(is_stutter_rate_at(ParamKind::UNPATCHED_GLOBAL, 0)).to_be_true();
		expect(is_stutter_rate_at(ParamKind::PATCHED, 0)).to_be_false();
		expect(is_bipolar_at(ParamKind::PATCH_CABLE, 0x0102U)).to_be_true();
		expect(is_bipolar_at(ParamKind::PATCHED, 23)).to_be_true();
		expect(is_bipolar_at(ParamKind::PATCHED, 2)).to_be_false();
		expect(is_bipolar_at(ParamKind::EXPRESSION, 0)).to_be_true();
	});

	it("finds the row of a sound's patched or unpatched param", _ {
		expect(param_row(ParamKind::PATCHED, param::kVolume)->id.value).to_equal(param::kVolume.value);
		expect(param_row(ParamKind::UNPATCHED_SOUND, param::kPortamento)->id.value).to_equal(param::kPortamento.value);
	});

	it("finds a global effectable's rows in the host chain's table and no patched row there", _ {
		expect(param_row(ParamKind::UNPATCHED_GLOBAL, deluge::host::chains::param::kTempo)->id.value)
		    .to_equal(deluge::host::chains::param::kTempo.value);
		expect(param_row(ParamKind::UNPATCHED_GLOBAL, param::kVolume)).to_be_null();
		expect(param_row(ParamKind::UNPATCHED_SOUND, param::kVolume)).to_be_null();
		expect(param_row(ParamKind::EXPRESSION, ParamId{0})).to_be_null();
	});

	it("knows the stutter rate under both unpatched kinds", _ {
		expect(is_stutter_rate(ParamKind::UNPATCHED_GLOBAL, deluge::host::chains::param::kStutterRate)).to_be_true();
		expect(is_stutter_rate(ParamKind::UNPATCHED_SOUND, deluge::host::chains::param::kStutterRate)).to_be_true();
		expect(is_stutter_rate(ParamKind::PATCHED, deluge::host::chains::param::kStutterRate)).to_be_false();
		expect(is_stutter_rate(ParamKind::UNPATCHED_SOUND, param::kPortamento)).to_be_false();
	});

	it("calls a centred param, a cable depth and the pitch-bend axis bipolar", _ {
		expect(is_bipolar(ParamKind::PATCHED, param::kPan)).to_be_true();
		expect(is_bipolar(ParamKind::UNPATCHED_GLOBAL, deluge::host::chains::param::kPan)).to_be_true();
		expect(is_bipolar(ParamKind::PATCH_CABLE, ParamId{0})).to_be_true();
		expect(is_bipolar(ParamKind::EXPRESSION, kExpressionPitchBend)).to_be_true();
	});

	it("calls an uncentred param, another expression axis and a host space bipolar-free", _ {
		expect(is_bipolar(ParamKind::PATCHED, param::kVolume)).to_be_false();
		expect(is_bipolar(ParamKind::UNPATCHED_SOUND, param::kPortamento)).to_be_false();
		expect(is_bipolar(ParamKind::EXPRESSION, ParamId{1})).to_be_false();
		expect(is_bipolar(ParamKind::MIDI, ParamId{0})).to_be_false();
	});
});
// clang-format on

CPPSPEC_SPEC(param_predicates)
