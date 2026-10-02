// tests/unit/io/stream_spec.cpp
#include "io/stream.hpp"
#include "libdeluge/channel.h"
#include "mock_stream_io.h"

#include "cppspec.hpp"

#include <string>

// clang-format off
describe stream("deluge::io::Stream", $ {
	it("refuses a path longer than DELUGE_PATH_MAX with PARAM, without reaching the platform", _ {
		mock_stream_io_reset();
		const std::string path(DELUGE_PATH_MAX, 'a');
		auto opened = deluge::io::Stream::open(path, DELUGE_STREAM_WRITE_CREATE);
		expect(opened.has_value()).to_equal(false);
		expect(opened.error()).to_equal(deluge::io::Status::PARAM);
		expect(mock_stream_io_opens()).to_equal(0u);
	});

	it("opens a path that fits", _ {
		mock_stream_io_reset();
		auto opened = deluge::io::Stream::open("REC.WAV", DELUGE_STREAM_WRITE_CREATE);
		expect(opened.has_value()).to_equal(true);
		expect(mock_stream_io_opens()).to_equal(1u);
	});
});

CPPSPEC_SPEC(stream)
