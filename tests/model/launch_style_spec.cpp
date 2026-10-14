#include "deluge-project/launch_style.h"

#include "cppspec.hpp"

#include <utility>

using deluge::project::LaunchStyle;
using deluge::project::parse_launch_style;
using deluge::project::to_string;

// clang-format off
describe launch_style("LaunchStyle", $ {
	it("keeps the enumerator values records store", _ {
		expect(std::to_underlying(LaunchStyle::DEFAULT)).to_equal(0);
		expect(std::to_underlying(LaunchStyle::FILL)).to_equal(1);
		expect(std::to_underlying(LaunchStyle::ONCE)).to_equal(2);
	});

	it("spells each style as the launchStyle attribute does", _ {
		expect(to_string(LaunchStyle::DEFAULT)).to_equal(std::string_view{"default"});
		expect(to_string(LaunchStyle::FILL)).to_equal(std::string_view{"fill"});
		expect(to_string(LaunchStyle::ONCE)).to_equal(std::string_view{"once"});
	});

	it("parses every spelling back to its style", _ {
		for (LaunchStyle style : {LaunchStyle::DEFAULT, LaunchStyle::FILL, LaunchStyle::ONCE}) {
			expect(parse_launch_style(to_string(style)).value()).to_equal(style);
		}
	});

	it("does not recognise other text, which callers read as DEFAULT", _ {
		expect(parse_launch_style("bogus")).not_().to_have_value();
		expect(parse_launch_style("")).not_().to_have_value();
		expect(parse_launch_style("Fill")).not_().to_have_value();
	});
});

CPPSPEC_SPEC(launch_style)
