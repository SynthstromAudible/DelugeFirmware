// tests/unit/audio_stream/read_source_spec.cpp
#include "mock_read_source.h"

#include "cppspec.hpp"

#include <array>
#include <cstddef>

using namespace deluge::audio::stream;

// clang-format off
describe read_source("ReadSource (mock)", $ {
	it("returns the requested cluster's bytes", _ {
		MockReadSource src{{{std::byte{1}, std::byte{2}}, {std::byte{3}, std::byte{4}}}};
		std::array<std::byte, 2> buf{};
		auto n = src.read(1, buf);
		expect(n.has_value()).to_equal(true);
		expect(n.value()).to_equal(2u);
		expect(std::to_integer<int>(buf[0])).to_equal(3);
		expect(std::to_integer<int>(buf[1])).to_equal(4);
	});

	it("errors on an out-of-range cluster index", _ {
		MockReadSource src{{{std::byte{1}}}};
		std::array<std::byte, 1> buf{};
		auto n = src.read(5, buf);
		expect(n.has_value()).to_equal(false);
	});
});

CPPSPEC_SPEC(read_source)
