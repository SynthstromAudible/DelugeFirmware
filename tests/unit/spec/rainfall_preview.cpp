/// Renders the Rainfall field to stdout as ASCII, so the animation can be eyeballed and its
/// constants tuned without flashing hardware.
///
/// Deliberately not a spec: it asserts nothing, and its filename avoids the `*_spec.cpp` glob
/// so ctest never picks it up. Keep it that way -- this is a visual tuning aid, not a
/// correctness test, and there is nothing meaningful to assert about ASCII art.
///
/// Usage: ./tests/build/spec/RainfallPreview [numFrames] [--logo]
///
/// `--logo` forces a logo emission immediately. Without it you would wait out the real interval,
/// which is minutes long by design, so the feature would be effectively uninspectable here.

#include "hid/display/rainfall.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using deluge::hid::display::Rainfall;

namespace {

/// One glyph per depth, so the parallax layers can be told apart by eye. The logo takes its own
/// glyph so a sighting is unmistakable in a wall of text, which the panel itself does not do.
char glyphFor(int32_t size, bool isLogo) {
	if (isLogo) {
		return (size == 3) ? '@' : ((size == 2) ? 'O' : 'o');
	}
	return (size == 3) ? '#' : ((size == 2) ? '+' : '.');
}

void plot(char canvas[Rainfall::kHeight][Rainfall::kWidth], const Rainfall::Block& block, char glyph) {
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

} // namespace

int main(int argc, char** argv) {
	int32_t numFrames = 40;
	bool forceLogo = false;
	for (int32_t arg = 1; arg < argc; arg++) {
		if (strcmp(argv[arg], "--logo") == 0) {
			forceLogo = true;
		}
		else {
			numFrames = atoi(argv[arg]);
		}
	}

	Rainfall field;
	if (forceLogo) {
		field.forceLogo();
	}

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
				plot(canvas, block, glyphFor(block.size, false));
			}
		}

		// Drawn last so the mark reads over the rain, matching how the panel composites it.
		if (field.logoActive()) {
			for (size_t cell = 0; cell < Rainfall::kLogoCells; cell++) {
				const Rainfall::Block block = field.logoCellAt(cell);
				plot(canvas, block, glyphFor(block.size, true));
			}
		}

		printf("--- frame %d%s ---\n", frame, field.logoActive() ? "  [logo]" : "");
		for (int32_t y = Rainfall::kTopmost; y < Rainfall::kHeight; y++) {
			fwrite(canvas[y], 1, Rainfall::kWidth, stdout);
			putchar('\n');
		}

		field.advance();
	}
	return 0;
}
