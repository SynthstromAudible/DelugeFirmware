/// Renders the Starfield to stdout as ASCII, so the ported animation can be eyeballed
/// without flashing hardware.
///
/// Not a spec, deliberately: it asserts nothing, and its filename avoids the `*_spec.cpp` glob
/// (see tests/spec/CMakeLists.txt) so ctest never picks it up. Judging whether the animation looks right means
/// watching frames scroll by, which nothing here can assert automatically — this is a look-and-tell
/// tool, not a target for `ctest`. Do not wire it into ctest or bolt on assertions to "fix" that.
///
/// Usage: ./tests/build/spec/StarfieldPreview [numFrames]

#include "hid/display/starfield.h"
#include <cstdio>
#include <cstdlib>

using deluge::hid::display::Starfield;

// Mirrors OLED_MAIN_WIDTH_PIXELS / OLED_MAIN_HEIGHT_PIXELS / OLED_MAIN_TOPMOST_PIXEL from
// src/RZA1/cpu_specific.h (128 / 48 / 5). Hardcoded rather than included, because that header
// lives under the target-only RZA1 tree and isn't on the host build's include path.
constexpr int32_t kWidth = 128;
constexpr int32_t kHeight = 48;
constexpr int32_t kTopmost = 5;

int main(int argc, char** argv) {
	const int32_t numFrames = (argc > 1) ? atoi(argv[1]) : 40;

	Starfield field;
	for (int32_t frame = 0; frame < numFrames; frame++) {
		char canvas[kHeight][kWidth];
		for (int32_t y = 0; y < kHeight; y++) {
			for (int32_t x = 0; x < kWidth; x++) {
				canvas[y][x] = ' ';
			}
		}

		for (size_t i = 0; i < Starfield::kNumStars; i++) {
			const Starfield::Projected star = field.project(i);
			for (int32_t dy = 0; dy < star.size; dy++) {
				for (int32_t dx = 0; dx < star.size; dx++) {
					const int32_t x = star.x + dx;
					const int32_t y = star.y + dy;
					if (x >= 0 && x < kWidth && y >= kTopmost && y < kHeight) {
						canvas[y][x] = (star.size == 2) ? '#' : '.';
					}
				}
			}
		}

		printf("--- frame %d ---\n", frame);
		for (int32_t y = kTopmost; y < kHeight; y++) {
			fwrite(canvas[y], 1, kWidth, stdout);
			putchar('\n');
		}

		field.advance();
	}
	return 0;
}
