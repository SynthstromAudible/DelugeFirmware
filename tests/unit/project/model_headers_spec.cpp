// Every model header must compile with only the standalone libraries on the include path.
#include "project/param_automation.h"
#include "project/param_edit.h"
#include "project/song.h"

#include <cppspec.hpp>

// clang-format off
describe model_headers("the model's headers", ${
	it("compile without the app", _ {
		expect(sizeof(deluge::project::DocAutoParam)).to_be_greater_than(0);
	});
});

CPPSPEC_SPEC(model_headers)
