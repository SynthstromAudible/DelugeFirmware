/// Renders the Rainfall field to stdout as ASCII, so the animation can be eyeballed and its
/// constants tuned without flashing hardware. Not a spec: it asserts nothing and is not run by
/// ctest.
///
/// Usage: ./tests/build/spec/RainfallPreview [numFrames]

#include "hid/display/rainfall.h"
#include <cstdio>
#include <cstdlib>

using deluge::hid::display::Rainfall;

int main(int argc, char** argv) {
	const int32_t numFrames = (argc > 1) ? atoi(argv[1]) : 40;

	Rainfall field;
	for (int32_t frame = 0; frame < numFrames; frame++) {
		char canvas[Rainfall::kHeight][Rainfall::kWidth];
		for (int32_t y = 0; y < Rainfall::kHeight; y++) {
			for (int32_t x = 0; x < Rainfall::kWidth; x++) {
				canvas[y][x] = ' ';
			}
		}

		for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
			for (size_t cell = 0; cell < field.lengthOf(drop); cell++) {
				const Rainfall::Block block = field.cellAt(drop, cell);
				// One glyph per depth, so the parallax layers can be told apart by eye.
				const char glyph = (block.size == 3) ? '#' : ((block.size == 2) ? '+' : '.');
				for (int32_t dy = 0; dy < block.size; dy++) {
					for (int32_t dx = 0; dx < block.size; dx++) {
						const int32_t x = block.x + dx;
						const int32_t y = block.y + dy;
						if (x >= 0 && x < Rainfall::kWidth && y >= Rainfall::kTopmost && y < Rainfall::kHeight) {
							canvas[y][x] = glyph;
						}
					}
				}
			}
		}

		printf("--- frame %d ---\n", frame);
		for (int32_t y = Rainfall::kTopmost; y < Rainfall::kHeight; y++) {
			fwrite(canvas[y], 1, Rainfall::kWidth, stdout);
			putchar('\n');
		}

		field.advance();
	}
	return 0;
}
