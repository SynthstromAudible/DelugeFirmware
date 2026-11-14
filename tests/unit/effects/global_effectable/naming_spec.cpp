#include "effects/global_effectable/naming.h"
#include "effects/global_effectable/params.h"

#include <cppspec.hpp>

using namespace deluge::global_effectable;

// clang-format off
describe naming("the provisional global-effectable table's names for its params", ${
	it("names a param by its file name, and back", _ {
		expect(kNaming.id_of({"volume"})->value).to_equal(param::kVolume.value);
		expect(kNaming.ref_of(param::kTempo).param).to_equal(std::string_view("tempo"));
	});

	it("names no cable's depth: these params have no cables", _ {
		expect(kNaming.id_of({"volume", "lfo1"})).not_().to_have_value();
	});

	it("has no rule for mod knobs", _ {
		expect(kNaming.mod_knob_target).to_be_null();
	});
});

CPPSPEC_SPEC(naming)
