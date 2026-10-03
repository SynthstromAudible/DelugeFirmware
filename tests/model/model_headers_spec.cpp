// Every model header must compile with only the standalone libraries on the include path.
#include "deluge-project/param_automation.h"
#include "deluge-project/param_edit.h"
#include "deluge-project/song.h"

#include <cppspec.hpp>

// clang-format off
describe model_headers("the model's headers", ${
	it("compile without the app", _ {
		expect(sizeof(deluge::project::DocAutoParam) > 0).to_equal(true);
	});
});

CPPSPEC_SPEC(model_headers)
