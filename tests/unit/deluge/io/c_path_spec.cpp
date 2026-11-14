#include "io/c_path.h"

#include <cstring>
#include <string>

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::io::CPath;

// clang-format off
describe cpath_group("CPath", ${
	it("a substring is terminated at its end", _ {
		const std::string whole = "SONGS/SONG001.XML trailing";
		auto path = CPath::from(std::string_view{whole}.substr(0, 17));
		expect(path.has_value()).to_be_true();
		if (!path) {
			return;
		}
		expect(std::string{path->c_str()}).to_equal(std::string{"SONGS/SONG001.XML"});
	});

	it("the longest path fits and one more is refused", _ {
		const std::string longest(DELUGE_PATH_MAX - 1, 'a');
		expect(CPath::from(longest).has_value()).to_be_true();
		if (!CPath::from(longest)) {
			return;
		}
		expect(std::strlen(CPath::from(longest)->c_str())).to_equal(DELUGE_PATH_MAX - 1);
		const std::string tooLong(DELUGE_PATH_MAX, 'a');
		expect(CPath::from(tooLong).has_value()).to_be_false();
	});

	it("an embedded nul is refused", _ {
		const std::string withNul("A\0B", 3);
		expect(CPath::from(withNul).has_value()).to_be_false();
	});

	it("the view is the path", _ {
		auto path = CPath::from("KITS/KIT000.XML");
		expect(std::string{path->view()}).to_equal(std::string{"KITS/KIT000.XML"});
	});
});

DELUGE_SPEC(c_path_spec, cpath_group)
