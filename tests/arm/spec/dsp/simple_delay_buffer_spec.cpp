#include "delay/simple/buffer.hpp"
#include "util/functions.h"
#include <cppspec.hpp>
#include <iostream>

template <typename T>
std::ostream& operator<<(std::ostream& os, std::span<T> span) {
	os << '[';
	for (auto&& [i, v] : span | std::views::enumerate) {
		os << span[i];
		if (i < span.size() - 1) {
			os << ", ";
		}
	}
	os << ']';
	return os;
}

// clang-format off
describe dsp_simple_delay_buffer("Simple delay buffer", $ {
	using namespace deluge::dsp::delay::simple;
	it("is empty on construction", _ {
		Buffer<4> buffer{4};
		expect(buffer.size()).to_equal(4);
		expect(buffer.pos()).to_equal(0);
		expect(buffer.max_delay).to_equal(4);
	});

	it("writes and reads a sample", _ {
		Buffer<4> buffer{4};
		buffer.Write(0, 1.0f);
		expect(buffer.Read(0)).to_equal(1.0f);
	});

	it("advances the write head", _ {
		Buffer<4> buffer{4};
		buffer.Advance();
		expect(buffer.pos()).to_equal(1);
	});

	it("can be read fractionally", _ {
		Buffer<4> buffer{4};
		buffer.Write(0, 1.0f);
		buffer.Write(1, 2.0f);
		buffer.Write(2, 3.0f);
		buffer.Write(3, 0.0f);
		buffer.PrepForInterpolate();
		expect(buffer.ReadFractional(0.5f)).to_equal(1.5f);
	});

	it("can have a GainRamp applied", _ {
		Buffer<4> buffer{4};
		buffer.Write(0, 1.0f);
		buffer.Write(1, 1.0f);
		buffer.Write(2, 1.0f);
		buffer.Write(3, 1.0f);
		buffer.ApplyGainRamp(deluge::dsp::blocks::GainRamp{0.0f, 0.75f});
		expect(buffer.Read(0)).to_equal(0.f);
		expect(buffer.Read(1)).to_equal(.25f);
		expect(buffer.Read(2)).to_equal(.5f);
		expect(buffer.Read(3)).to_equal(0.75f);
	});

	it("can have a GainRamp applied at an offset", _ {
		Buffer<4> buffer{4};
		buffer.Write(0, 1.0f);
		buffer.Write(1, 1.0f);
		buffer.Write(2, 1.0f);
		buffer.Write(3, 1.0f);
		buffer.Advance();
		buffer.ApplyGainRamp(deluge::dsp::blocks::GainRamp{0.0f, 0.75f});
		expect(buffer.Read(0)).to_equal(0.f);
		expect(buffer.Read(1)).to_equal(.25f);
		expect(buffer.Read(2)).to_equal(.5f);
		expect(buffer.Read(3)).to_equal(0.75f);
	});

	it("can copy from one buffer to another of the same size", _ {
		Buffer<4> buffer{4};
		buffer.Write(0, 1.0f);
		buffer.Write(1, 2.0f);
		buffer.Write(2, 3.0f);
		buffer.Write(3, 4.0f);
		Buffer<4> other{};
		other.CopyFrom(buffer);
		expect(other.Read(0)).to_equal(1.0f);
		expect(other.Read(1)).to_equal(2.0f);
		expect(other.Read(2)).to_equal(3.0f);
		expect(other.Read(3)).to_equal(4.0f);
	});

	it("can copy from one buffer to another of a smaller size", _ {
		Buffer<4> buffer{4};
		buffer.Write(0, 1.0f);
		buffer.Write(1, 2.0f);
		buffer.Write(2, 3.0f);
		buffer.Write(3, 4.0f);
		Buffer<2> other{};
		other.CopyFrom(buffer);
		expect(other.Read(0)).to_equal(3.0f);
		expect(other.Read(1)).to_equal(4.0f);
	});

	it("can copy when advanced", _ {
		Buffer<4> buffer{4};
		buffer.Write(0, 1.0f);
		buffer.Write(1, 2.0f);
		buffer.Write(2, 3.0f);
		buffer.Write(3, 4.0f);
		buffer.Advance(4);
		Buffer<4> other{};
		other.CopyFrom(buffer);
		expect(other.Read(0)).to_equal(1.0f);
		expect(other.Read(1)).to_equal(2.0f);
		expect(other.Read(2)).to_equal(3.0f);
		expect(other.Read(3)).to_equal(4.0f);
	});
});

CPPSPEC_SPEC(dsp_simple_delay_buffer);
