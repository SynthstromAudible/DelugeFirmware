#include "deluge-project/sequence_direction.h"

#include "cppspec.hpp"

#include <utility>

using deluge::project::parse_sequence_direction;
using deluge::project::SequenceDirection;
using deluge::project::to_string;

// clang-format off
describe sequence_direction("SequenceDirection", $ {
	it("keeps the enumerator values records store", _ {
		expect(std::to_underlying(SequenceDirection::FORWARD)).to_equal(0);
		expect(std::to_underlying(SequenceDirection::REVERSE)).to_equal(1);
		expect(std::to_underlying(SequenceDirection::PINGPONG)).to_equal(2);
		expect(std::to_underlying(SequenceDirection::OBEY_PARENT)).to_equal(3);
	});

	it("spells each direction as the sequenceDirection attribute does", _ {
		expect(to_string(SequenceDirection::FORWARD)).to_equal(std::string_view{"forward"});
		expect(to_string(SequenceDirection::REVERSE)).to_equal(std::string_view{"reverse"});
		expect(to_string(SequenceDirection::PINGPONG)).to_equal(std::string_view{"pingpong"});
		expect(to_string(SequenceDirection::OBEY_PARENT)).to_equal(std::string_view{"obeyParent"});
	});

	it("parses every spelling back to its direction", _ {
		for (SequenceDirection d : {SequenceDirection::FORWARD, SequenceDirection::REVERSE,
		                            SequenceDirection::PINGPONG, SequenceDirection::OBEY_PARENT}) {
			expect(parse_sequence_direction(to_string(d)).value()).to_equal(d);
		}
	});

	it("does not recognise other text, which callers read as FORWARD", _ {
		expect(parse_sequence_direction("none").has_value()).to_equal(false);
		expect(parse_sequence_direction("bogus").has_value()).to_equal(false);
	});
});

CPPSPEC_SPEC(sequence_direction)
