#include "util/semver.h"

#include "cppspec.hpp"

describe semver(
    "SemVer", $ {
	    it(
	        "parses a simple semver", _ {
		        std::expected parse_result = SemVer::parse("1.2.4");
		        expect(parse_result).to_have_value();
		        expect(parse_result.value()).to_equal(SemVer{1, 2, 4});
	        });

	    it(
	        "parses a semver with a pre-release tag",
	        _ { expect(SemVer::parse("7.2.6-a92c49").value()).to_equal(SemVer{7, 2, 6, "a92c49"}); });

	    context(
	        "with an alpha pre-release version number", _ {
		        auto pre_release = SemVer{1, 5, 2, "alpha"};
		        it("is less than its full release", _ { expect(pre_release).to_be_less_than(SemVer{1, 5, 2}); });

		        it(
		            "is less than its beta release",
		            _ { expect(pre_release).to_be_less_than(SemVer{1, 5, 2, "beta"}); });

		        it(
		            "is greater than the previous core version",
		            _ { expect(pre_release).to_be_greater_than(SemVer{1, 5, 1}); });
	        });
    });

CPPSPEC_SPEC(semver)
