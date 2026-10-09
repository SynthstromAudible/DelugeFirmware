#include "io/debug/fault_pattern.h"
#include "libdeluge/fault.h"

#include "cppspec.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// The board side of libdeluge/fault.h, recording what the renderer sends to the pad driver. Code is
// [kCodeStart, kCodeEnd); the stack is whichever array a spec walks.
namespace {

constexpr uint32_t kCodeStart = 0x1000;
constexpr uint32_t kCodeEnd = 0x8000;

std::vector<uint8_t> sent;
int flushes = 0;
std::span<uint32_t> stack;

/// One column pair as the PIC receives it: a select byte, then 16 pads of RGB.
struct ColumnPair {
	uint8_t select = 0;
	std::array<std::array<uint8_t, 3>, 16> pads{};
};

/// A drawn value: the bits of the lit pads, in their colour (black when nothing is lit).
struct Drawn {
	uint32_t value = 0;
	std::string colour = "black";
};

std::string colourName(std::array<uint8_t, 3> rgb) {
	if (rgb == std::array<uint8_t, 3>{0, 0, 0}) {
		return "black";
	}
	if (rgb == std::array<uint8_t, 3>{255, 0, 255}) {
		return "magenta";
	}
	if (rgb == std::array<uint8_t, 3>{0, 0, 255}) {
		return "blue";
	}
	if (rgb == std::array<uint8_t, 3>{0, 255, 0}) {
		return "green";
	}
	if (rgb == std::array<uint8_t, 3>{0, 255, 255}) {
		return "cyan";
	}
	if (rgb == std::array<uint8_t, 3>{255, 0, 0}) {
		return "red";
	}
	if (rgb == std::array<uint8_t, 3>{255, 255, 0}) {
		return "yellow";
	}
	if (rgb == std::array<uint8_t, 3>{255, 255, 255}) {
		return "white";
	}
	return "other";
}

std::vector<ColumnPair> columnPairs() {
	std::vector<ColumnPair> pairs;
	size_t at = 0;
	while (at + 1 + 16 * 3 <= sent.size()) {
		ColumnPair pair;
		pair.select = sent[at++];
		for (auto& pad : pair.pads) {
			pad = {sent[at], sent[at + 1], sent[at + 2]};
			at += 3;
		}
		pairs.push_back(pair);
	}
	return pairs;
}

/// Reads the given pairs' pads, most significant bit first, as one number.
Drawn read(std::span<ColumnPair const> pairs) {
	Drawn drawn;
	for (ColumnPair const& pair : pairs) {
		for (auto const& pad : pair.pads) {
			drawn.value <<= 1;
			if (pad != std::array<uint8_t, 3>{0, 0, 0}) {
				drawn.value |= 1;
				drawn.colour = colourName(pad);
			}
		}
	}
	return drawn;
}

/// The 32-bit pointer in main-grid slot `slot` (two column pairs each).
Drawn pointerAt(std::vector<ColumnPair> const& pairs, size_t slot) {
	return read(std::span{pairs}.subspan(slot * 2, 2));
}

/// The commit ID's first two bytes on the sidebar, the ninth column pair.
Drawn commit(std::vector<ColumnPair> const& pairs) {
	return read(std::span{pairs}.subspan(8, 1));
}

uint32_t address(uint32_t& word) {
	return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&word));
}

void reset() {
	sent.clear();
	flushes = 0;
	stack = {};
}

} // namespace

extern "C" {
char const* kCommitShort = "a1b2";

void deluge_fault_ranges(DelugeFaultRanges* out) noexcept {
	*out = DelugeFaultRanges{};
	out->code_start = kCodeStart;
	out->code_end = kCodeEnd;
	if (!stack.empty()) {
		out->stack_start = reinterpret_cast<uintptr_t>(stack.data());
		out->stack_end = reinterpret_cast<uintptr_t>(stack.data() + stack.size());
	}
}

void deluge_fault_pad_write(uint8_t byte) noexcept {
	sent.push_back(byte);
}

void deluge_fault_pad_flush() noexcept {
	flushes++;
}
}

// clang-format off
describe fault_pattern("deluge_fault_pattern_draw", $ {
	before_each([] { reset(); });

	it("draws the faulting instruction, the caller and the stack's return addresses, then the commit", _{
		std::array<uint32_t, 8> words{0x3001, 0, 0, 0x4001, 0, 0, 0, 0};
		stack = words;
		deluge_fault_pattern_draw(0x1234, DELUGE_CPU_FAULT_DATA_ABORT, 0x2001, address(words[0]));

		auto pairs = columnPairs();
		expect(pairs.size()).to_equal(size_t{9});
		for (size_t i = 0; i < pairs.size(); i++) {
			expect(static_cast<int>(pairs[i].select)).to_equal(static_cast<int>(i + 1));
		}
		expect(pointerAt(pairs, 0).value).to_equal(uint32_t{0x1234});
		expect(pointerAt(pairs, 0).colour).to_equal(std::string{"magenta"});
		expect(pointerAt(pairs, 1).value).to_equal(uint32_t{0x1fff});
		expect(pointerAt(pairs, 1).colour).to_equal(std::string{"blue"});
		expect(pointerAt(pairs, 2).value).to_equal(uint32_t{0x2fff});
		expect(pointerAt(pairs, 2).colour).to_equal(std::string{"green"});
		expect(pointerAt(pairs, 3).value).to_equal(uint32_t{0x3fff});
		expect(pointerAt(pairs, 3).colour).to_equal(std::string{"cyan"});
		expect(commit(pairs).value).to_equal(uint32_t{0xa1b2});
		expect(flushes).to_equal(1);
	});

	it("leaves out a faulting instruction that isn't code, as a call through a null pointer gives", _{
		deluge_fault_pattern_draw(0x4, DELUGE_CPU_FAULT_PREFETCH_ABORT, 0x2001, 0);

		auto pairs = columnPairs();
		expect(pointerAt(pairs, 0).value).to_equal(uint32_t{0x1fff});
		expect(pointerAt(pairs, 0).colour).to_equal(std::string{"blue"});
		expect(pointerAt(pairs, 1).value).to_equal(uint32_t{0});
	});

	it("draws a freeze from its caller, with no faulting instruction", _{
		fault_handler_print_freeze_pointers(0x2001, 0);

		auto pairs = columnPairs();
		expect(pointerAt(pairs, 0).value).to_equal(uint32_t{0x1fff});
		expect(pointerAt(pairs, 0).colour).to_equal(std::string{"blue"});
		expect(commit(pairs).colour).to_equal(std::string{"yellow"});
	});

	it("colours the commit by fault type", _{
		struct Case {
			DelugeCpuFault type;
			std::string colour;
		};
		for (Case const& c : {Case{DELUGE_CPU_FAULT_UNDEFINED, "green"}, Case{DELUGE_CPU_FAULT_PREFETCH_ABORT, "blue"},
		                      Case{DELUGE_CPU_FAULT_DATA_ABORT, "red"}, Case{DELUGE_CPU_FAULT_RESERVED, "magenta"},
		                      Case{DELUGE_CPU_FAULT_FREEZE, "yellow"}, Case{DELUGE_CPU_FAULT_SVC, "cyan"}}) {
			reset();
			deluge_fault_pattern_draw(0, c.type, 0, 0);
			expect(commit(columnPairs()).colour).to_equal(c.colour);
		}
	});

	it("skips a stack word that repeats the last return address found", _{
		std::array<uint32_t, 12> words{0x3001, 0, 0, 0x4001, 0, 0, 0x4001, 0x5001, 0, 0, 0, 0};
		stack = words;
		deluge_fault_pattern_draw(0, DELUGE_CPU_FAULT_DATA_ABORT, 0, address(words[0]));

		auto pairs = columnPairs();
		expect(pointerAt(pairs, 0).value).to_equal(uint32_t{0x2fff});
		expect(pointerAt(pairs, 1).value).to_equal(uint32_t{0x3fff});
		expect(pointerAt(pairs, 2).value).to_equal(uint32_t{0x4fff});
		expect(pointerAt(pairs, 3).value).to_equal(uint32_t{0});
	});

	it("skips stack words that are the caller or the faulting instruction, already drawn", _{
		std::array<uint32_t, 8> words{0x2001, 0x1234, 0x3001, 0, 0, 0, 0, 0};
		stack = words;
		deluge_fault_pattern_draw(0x1234, DELUGE_CPU_FAULT_DATA_ABORT, 0x2001, address(words[0]));

		auto pairs = columnPairs();
		expect(pointerAt(pairs, 2).value).to_equal(uint32_t{0x2fff});
		expect(pointerAt(pairs, 3).value).to_equal(uint32_t{0});
	});
});

CPPSPEC_SPEC(fault_pattern)
