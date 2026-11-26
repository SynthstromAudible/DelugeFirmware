// project::Iterance: the packed form a song stores, the presets the menus step through, which repeats a note plays,
// and the labels the menus show. The labels are those the legacy Iterance gave, captured when it was replaced.
#include "project/iterance.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "support/spec_main.h"

using deluge::project::format_preset;
using deluge::project::Iterance;

namespace {

// Preset index 0 to 39: OFF, the 37 presets, CUSTOM, then one past CUSTOM.
constexpr std::array<std::string_view, 40> kLongLabels{
    "OFF",    "FIRST",  "LAST",   "1 of 2", "2 of 2", "1 of 3", "2 of 3", "3 of 3", "1 of 4", "2 of 4",
    "3 of 4", "4 of 4", "1 of 5", "2 of 5", "3 of 5", "4 of 5", "5 of 5", "1 of 6", "2 of 6", "3 of 6",
    "4 of 6", "5 of 6", "6 of 6", "1 of 7", "2 of 7", "3 of 7", "4 of 7", "5 of 7", "6 of 7", "7 of 7",
    "1 of 8", "2 of 8", "3 of 8", "4 of 8", "5 of 8", "6 of 8", "7 of 8", "8 of 8", "CUSTOM", "OFF",
};
constexpr std::array<std::string_view, 40> kShortLabels{
    "OFF", "1ST", "LAST", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4",    "4:4", "1:5", "2:5",
    "3:5", "4:5", "5:5",  "1:6", "2:6", "3:6", "4:6", "5:6", "6:6", "1:7", "2:7",    "3:7", "4:7", "5:7",
    "6:7", "7:7", "1:8",  "2:8", "3:8", "4:8", "5:8", "6:8", "7:8", "8:8", "CUSTOM", "OFF",
};

std::string label(int32_t index, std::string_view separator, Iterance::Label kind) {
	std::array<char, 30> buffer{};
	const std::size_t length = format_preset(index, buffer, "", separator, kind);
	return {buffer.data(), length};
}

} // namespace

// clang-format off
describe iterance_spec_body("project::Iterance", ${
	it("packs the divisor high and the steps low, and unpacks every valid iterance to itself", _ {
		expect(int32_t{Iterance{.divisor = 4, .steps = 0b1001}.packed()}).to_equal(0x0409);
		int32_t valid = 0;
		for (int32_t v = 0; v <= 0xFFFF; ++v) {
			const Iterance iterance = Iterance::from_packed(v);
			if (int32_t{iterance.packed()} == v) {
				++valid;
			}
		}
		// Divisors 1 to 8 with any steps, and divisor 0 with first or last.
		expect(valid).to_equal(8 * 256 + 2 + 1);
	});

	it("reads a divisor above 8, or a divisor of 0 with neither first nor last, as OFF", _ {
		expect(int32_t{Iterance::from_packed(0x0901).packed()}).to_equal(int32_t{deluge::project::kIteranceOff.packed()});
		expect(int32_t{Iterance::from_packed(0x0003).packed()}).to_equal(int32_t{deluge::project::kIteranceOff.packed()});
		expect(int32_t{Iterance::from_packed(0x0001).packed()}).to_equal(int32_t{deluge::project::kIteranceFirst.packed()});
	});

	it("names each preset by its index, CUSTOM for any other iterance, and OFF for none", _ {
		for (int32_t i = 1; i <= deluge::project::kIterancePresetCount; ++i) {
			expect(Iterance::from_preset_index(i).preset_index()).to_equal(i);
		}
		expect(deluge::project::kIteranceOff.preset_index()).to_equal(0);
		expect(Iterance{.divisor = 4, .steps = 0b1001}.preset_index()).to_equal(deluge::project::kCustomIterancePreset);
		expect(int32_t{Iterance::from_preset_index(deluge::project::kCustomIterancePreset).packed()}).to_equal(int32_t{deluge::project::kIteranceCustom.packed()});
		expect(int32_t{Iterance::from_preset_index(-1).packed()}).to_equal(int32_t{deluge::project::kIteranceOff.packed()});
		expect(int32_t{Iterance::from_preset_index(40).packed()}).to_equal(int32_t{deluge::project::kIteranceOff.packed()});
	});

	it("orders the presets first, last, then each step of 2 to 8 repeats", _ {
		expect(int32_t{Iterance::from_preset_index(1).packed()}).to_equal(0x0001);
		expect(int32_t{Iterance::from_preset_index(2).packed()}).to_equal(0x0002);
		expect(int32_t{Iterance::from_preset_index(3).packed()}).to_equal(0x0201);
		expect(int32_t{Iterance::from_preset_index(4).packed()}).to_equal(0x0202);
		expect(int32_t{Iterance::from_preset_index(5).packed()}).to_equal(0x0301);
		expect(int32_t{Iterance::from_preset_index(37).packed()}).to_equal(0x0880);
		for (int32_t i = 3; i <= deluge::project::kIterancePresetCount; ++i) {
			const Iterance preset = Iterance::from_preset_index(i);
			expect(int32_t{preset.divisor}).to_be_greater_than(1);
			expect(preset.steps.count()).to_equal(std::size_t{1});
		}
	});

	it("plays every repeat for OFF, the first, the last, or each repeat whose step plays", _ {
		expect(deluge::project::kIteranceOff.passes(5, false)).to_equal(true);
		expect(deluge::project::kIteranceFirst.passes(0, false)).to_equal(true);
		expect(deluge::project::kIteranceFirst.passes(1, true)).to_equal(false);
		expect(deluge::project::kIteranceLast.passes(5, true)).to_equal(true);
		expect(deluge::project::kIteranceLast.passes(5, false)).to_equal(false);
		const Iterance two_of_three{.divisor = 3, .steps = 0b10};
		expect(two_of_three.passes(4, false)).to_equal(true);
		expect(two_of_three.passes(5, false)).to_equal(false);
	});

	it("steps a cycle over the whole count, with no wrap", _ {
		// 4294967295 is 0 mod 3, so a 3of3 iterance does not play it and a 1of3 does.
		constexpr uint32_t kLast = std::numeric_limits<uint32_t>::max();
		expect(Iterance{.divisor = 3, .steps = 0b100}.passes(kLast, false)).to_equal(false);
		expect(Iterance{.divisor = 3, .steps = 0b1}.passes(kLast, false)).to_equal(true);
	});

	it("labels every preset as the menus show it", _ {
		for (int32_t i = 0; i < 40; ++i) {
			expect(label(i, " of ", Iterance::Label::LONG)).to_equal(std::string(kLongLabels[static_cast<std::size_t>(i)]));
			expect(label(i, ":", Iterance::Label::SHORT)).to_equal(std::string(kShortLabels[static_cast<std::size_t>(i)]));
		}
		expect(label(-1, " of ", Iterance::Label::LONG)).to_equal(std::string("OFF"));
	});

	it("truncates a label to its buffer and always terminates it", _ {
		std::array<char, 4> small{'x', 'x', 'x', 'x'};
		const std::size_t length = format_preset(deluge::project::kCustomIterancePreset, small, "", ":", Iterance::Label::SHORT);
		expect(length).to_equal(std::size_t{3});
		expect(std::string(small.data())).to_equal(std::string("CUS"));
		std::array<char, 0> none{};
		expect(format_preset(1, none, "", ":", Iterance::Label::SHORT)).to_equal(std::size_t{0});
	});
});
// clang-format on

DELUGE_SPEC(iterance_spec, iterance_spec_body)
