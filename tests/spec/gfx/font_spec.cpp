#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "gfx/oled/font.h"

using namespace deluge::gfx::oled;

// clang-format off
describe font("gfx::oled fonts", $ {
	it("maps text heights to fonts exactly as the canvas did", _ {
		expect(font_for_text_height(5).id).to_equal(FontId::Px5);
		expect(font_for_text_height(6).id).to_equal(FontId::Px5);
		expect(font_for_text_height(7).id).to_equal(FontId::Apple7);
		expect(font_for_text_height(8).render_height).to_equal(7);
		expect(font_for_text_height(9).y_adjust).to_equal(1);
		expect(font_for_text_height(10).id).to_equal(FontId::MetricBold9);
		expect(font_for_text_height(13).id).to_equal(FontId::MetricBold13);
		expect(font_for_text_height(20).id).to_equal(FontId::MetricBold20);
		expect(font_for_text_height(99).id).to_equal(FontId::MetricBold20);
		expect(font_for_text_height(8).monospaced).to_be_true();
		expect(font_for_text_height(13).monospaced).to_be_false();
	});

	it("leaves render_height at the sentinel 0 for the default (MetricBold20) font, so the "
	   "caller's own text height survives unchanged, as the legacy switch did", _ {
		expect(font_for_text_height(15).render_height).to_equal(0);
		expect(font_for_text_height(20).render_height).to_equal(0);
	});

	it("indexes glyphs with lowercase folded onto uppercase", _ {
		expect(glyph_index('A')).to_equal(glyph_index('a'));
		expect(glyph_index(' ')).to_equal(0);
		expect(glyph_index('A')).to_equal('A' - 0x20);
	});
});

DELUGE_SPEC(gfx_font_spec, font)
