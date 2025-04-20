#include "util/string.h"
#include <cppspec.hpp>

extern "C" void putchar_(char c) {
	putchar(c);
}

// clang-format off
describe string("deluge::string", ${
	using namespace deluge;
	using namespace std::literals;
	context("fromInt", _ {
		it("converts an integer into a string", _ {
			expect(string::fromInt(42)).to_equal("42"s);
		});

		it("left-pads with '0's", _ {
			expect(string::fromInt(42, 3)).to_equal("042"s);
		});
	});

	context("fromFloat", _ {
		it("converts a float into a string", _ {
			expect(string::fromFloat(3.14, 2)).to_equal("3.14"s);
		});

		it("rounds to the given precision", _ {
			expect(string::fromFloat(3.14159, 3)).to_equal("3.142"s);
		});
	});

	context("fromSlot", _ {
		it("converts a slot and sub-slot into a string", _ {
			expect(string::fromSlot(3, 1)).to_equal("3B"s);
		});

		it("left-pads the slot with '0's", _ {
			expect(string::fromSlot(3, 1, 3)).to_equal("003B"s);
		});
	});

	context("fromNoteCode", _ {
		it("converts a note code into a string", _ {
			expect(string::fromNoteCode(60)).to_equal("C3"s);
		});

		it("appends the octave number", _ {
			expect(string::fromNoteCode(63, nullptr, true)).to_equal("D.3"s);
		});

		it("does not append the octave number", _ {
			expect(string::fromNoteCode(60, nullptr, false)).to_equal("C"s);
		});

		it("returns the length without the dot", _ {
			size_t length;
			expect(string::fromNoteCode(63, &length)).to_equal("D.3"s);
			expect(length).to_equal(2);
		});
	});
});

CPPSPEC_MAIN(string);
