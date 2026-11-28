#include "ui/widgets/oled/keys.h"

#include <array>

namespace deluge::ui::widgets::oled {

namespace {

constexpr std::array<int32_t, 7> kWhiteSemitones{0, 2, 4, 5, 7, 9, 11};
/// The black keys' semitones, and the white key each sits to the right of.
constexpr std::array<int32_t, 5> kBlackSemitones{1, 3, 6, 8, 10};
constexpr std::array<int32_t, 5> kBlackAfterWhite{0, 1, 3, 4, 5};

void fill(OledFrame& f, gfx::oled::Rect r) {
	for (int32_t y = r.y; y < r.y + r.h; y++) {
		f.hline(y, r.x, r.x + r.w - 1);
	}
}

bool in_scale(uint16_t mask, int32_t semitone) {
	return ((mask >> semitone) & 1u) != 0;
}

} // namespace

void octave(OledFrame& f, gfx::oled::Rect r, uint16_t scale_mask, int32_t root) {
	const int32_t ww = (r.w - 1) / 7; // keys share edges: 7 keys are 7 * ww + 1 wide
	const int32_t bw = ww * 2 / 3;
	const int32_t bh = r.h * 3 / 5;
	for (int32_t i = 0; i < 7; i++) {
		const gfx::oled::Rect key{r.x + i * ww, r.y, ww + 1, r.h};
		f.rect(key);
		if (in_scale(scale_mask, kWhiteSemitones[i])) {
			fill(f, {key.x + 2, key.y + 2, key.w - 4, key.h - 4}); // inset: filled neighbours stay apart
		}
		if (kWhiteSemitones[i] == root) {
			f.hline(r.y + r.h, key.x, key.x + key.w - 1);
		}
	}
	for (size_t i = 0; i < kBlackSemitones.size(); i++) {
		const gfx::oled::Rect key{r.x + (kBlackAfterWhite[i] + 1) * ww - bw / 2, r.y, bw, bh};
		f.clear(key);
		if (in_scale(scale_mask, kBlackSemitones[i])) {
			fill(f, {key.x + 1, key.y + 1, key.w - 2, key.h - 2});
		}
		else {
			f.rect(key);
		}
		if (kBlackSemitones[i] == root) {
			f.hline(r.y + r.h, key.x, key.x + key.w - 1);
		}
	}
}

void zone_strip(OledFrame& f, gfx::oled::Rect r, std::span<const uint8_t> zone_tops, int32_t edited) {
	const auto x_of = [&](int32_t note) { return r.x + note * r.w / 128; };
	for (int32_t note = 0; note < 128; note += 12) {
		for (int32_t y = r.y; y < r.y + r.h; y += 2) {
			f.pixel(x_of(note), y);
		}
	}
	int32_t first = 0;
	for (size_t z = 0; z < zone_tops.size(); z++) {
		const int32_t top = z + 1 == zone_tops.size() ? 127 : zone_tops[z];
		const int32_t x0 = x_of(first);
		const int32_t x1 = x_of(top + 1) - 1;
		if (x1 >= x0) {
			const gfx::oled::Rect box{x0, r.y + 2, x1 - x0 + 1, r.h - 4};
			f.clear(box);
			if (static_cast<int32_t>(z) == edited) {
				fill(f, box);
			}
			else {
				f.rect(box);
			}
		}
		first = top + 1;
	}
}

} // namespace deluge::ui::widgets::oled
