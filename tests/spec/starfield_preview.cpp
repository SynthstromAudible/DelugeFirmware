/// Renders the Starfield to stdout as ASCII, so the ported animation can be eyeballed
/// without flashing hardware. Not a spec: it asserts nothing and is not run by ctest.
///
/// Usage: ./tests/build/spec/StarfieldPreview [numFrames]

#include "hid/display/starfield.h"
#include <cstdio>
#include <cstdlib>

using deluge::hid::display::Starfield;

// Matches the real panel: OLED_MAIN_WIDTH_PIXELS / HEIGHT_PIXELS / TOPMOST_PIXEL.
// Hardcoded rather than included, because cpu_specific.h is target-only.
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
