#include "host/chains/naming.h"
#include "host/chains/params.h"

#include <cppspec.hpp>

#include "support/printers/param_id_printer.h"

using namespace deluge::host::chains;

// clang-format off
describe naming("the host chain's names for its params", ${
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
