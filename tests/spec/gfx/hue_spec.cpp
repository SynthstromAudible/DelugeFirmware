#include "cppspec.hpp"
#include "gui/colour/rgb.h"
#include "support/golden.h"

#include "../support/spec_main.h"
#include <sstream>

namespace {
// hue wraps mod 192 inside fromHue/fromHuePastel, so 0..191 covers every distinct output.
constexpr int32_t kHueCount = 192;

void append_hue_line(std::ostringstream& out, int32_t hue) {
	RGB c = RGB::fromHue(hue);
	out << hue << " " << (int)c.r << " " << (int)c.g << " " << (int)c.b << "\n";
}

void append_pastel_line(std::ostringstream& out, int32_t hue) {
	RGB c = RGB::fromHuePastel(hue);
	out << "p " << hue << " " << (int)c.r << " " << (int)c.g << " " << (int)c.b << "\n";
}
} // namespace

// clang-format off
describe hue("RGB::fromHue / fromHuePastel golden", $ {
	it("matches the recorded golden for every hue 0..191", _ {
		std::ostringstream out;
		for (int32_t h = 0; h < kHueCount; h++) {
			append_hue_line(out, h);
		}
		for (int32_t h = 0; h < kHueCount; h++) {
			append_pastel_line(out, h);
		}
		expect_golden(self, "grid/hue.txt", out.str());
	});
});

DELUGE_SPEC(gfx_hue_spec, hue)
