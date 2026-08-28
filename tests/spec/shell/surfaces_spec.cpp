#include "support/fake_surfaces.h"

#include "cppspec.hpp"

#include "../support/spec_main.h"
using namespace deluge::ui::shell;

// clang-format off
describe shell_surfaces("shell::SurfaceSet", $ {
	it("sizes OLED surfaces from the descriptor and the profile's roles", _ {
		uitest::FakeSurfaceSet s(uitest::kBoard2, uitest::test_profile_two_oleds());
		expect(s.oled().size()).to_equal(size_t(2));
		expect(s.oled()[1].geometry.height).to_equal(uint16_t(64));
		expect(s.oled()[0].geometry.visible_top).to_equal(uint8_t(5));
		expect(s.oled(SurfaceRole::Main) != nullptr).to_be_true();
		expect(s.oled(SurfaceRole::Aux)->index).to_equal(uint8_t(1));
		expect(s.oled(SurfaceRole::Param0) == nullptr).to_be_true();
		expect(s.grid().grid().dims().main_cols).to_equal(uint8_t(16));
		expect(s.grid().grid().dims().sidebar_cols).to_equal(uint8_t(2));
		expect(s.grid().pairs).to_equal(uint8_t(9));
	});
	it("copies a style in for every surface", _ {
		uitest::FakeSurfaceSet s(uitest::kBoard2, uitest::test_profile_two_oleds());
		expect(s.oled()[0].style == &deluge::ui::kDelugeStyle48).to_be_true();
		expect(s.oled()[1].style == &deluge::ui::kDelugeStyle64).to_be_true();
	});
	it("falls back to display_width/height when the board lists no panels", _ {
		DelugeBoard b = uitest::kBoard2;
		b.displays = nullptr;
		b.display_count = 1;
		uitest::FakeSurfaceSet s(b, uitest::test_profile_two_oleds());
		expect(s.oled().size()).to_equal(size_t(1));
		expect(s.oled()[0].geometry.width).to_equal(uint16_t(128));
		expect(s.oled()[0].geometry.height).to_equal(uint16_t(48));
		expect(s.oled()[0].geometry.visible_top).to_equal(uint8_t(0));
	});
	it("clamps a board with more displays than the pool holds", _ {
		DelugeBoard b = uitest::kBoard2;
		b.display_count = kMaxOled + 3;
		uitest::FakeSurfaceSet s(b, uitest::test_profile_two_oleds());
		expect(s.oled().size()).to_equal(size_t(kMaxOled));
	});
	it("gives surfaces past the profile's spans no role and no style", _ {
		DelugeBoard b = uitest::kBoard2;
		b.displays = nullptr;
		b.display_count = 3;
		uitest::FakeSurfaceSet s(b, uitest::test_profile_two_oleds());
		expect(s.oled()[2].role == SurfaceRole::None).to_be_true();
		expect(s.oled()[2].style == nullptr).to_be_true();
		expect(s.oled(SurfaceRole::None) == nullptr).to_be_true();
	});
	it("compares column pairs by value", _ {
		ColumnPair a{}, b{};
		expect(a == b).to_be_true();
		b.colours[7] = RGB{1, 2, 3};
		expect(a == b).to_be_false();
	});
	it("records what the shell would flush", _ {
		uitest::FakeSurfaceSet s(uitest::kBoard2, uitest::test_profile_two_oleds());
		ColumnPair p{};
		p.colours[3] = RGB{1, 2, 3};
		s.send_columns(4, p);
		s.set_led(7, true);
		s.set_indicator(0, deluge::ui::KnobIndicator{{3, 3, 2, 0}});
		s.flush();
		expect(s.columns_sent.size()).to_equal(size_t(1));
		expect(s.columns_sent[0].first).to_equal(uint8_t(4));
		expect(s.columns_sent[0].second == p).to_be_true();
		expect(s.led_writes.size()).to_equal(size_t(1));
		expect(s.indicator_writes.size()).to_equal(size_t(1));
		expect(s.flushes).to_equal(1);
	});
	it("records blits and renders a surface as a PBM", _ {
		uitest::FakeSurfaceSet s(uitest::kBoard2, uitest::test_profile_two_oleds());
		expect(s.oled_busy(0)).to_be_false();
		s.busy[0] = true;
		expect(s.oled_busy(0)).to_be_true();
		expect(s.pad_output_space()).to_equal(uint32_t(1u << 20));

		s.oled()[0].canvas.clear();
		s.oled()[0].canvas.draw_pixel(0, 0);
		s.blit(0, s.oled()[0].canvas.bytes(), 128, 48);
		expect(s.blits.size()).to_equal(size_t(1));
		expect(s.blits[0].index).to_equal(uint8_t(0));
		expect(s.blits[0].byte_count).to_equal(size_t(128 * 48 / 8));
		expect(s.pbm(0).starts_with("P1\n128 48\n1 0")).to_be_true();
	});
	it("reports pads, LEDs and indicators in the snapshot format", _ {
		uitest::FakeSurfaceSet s(uitest::kBoard2, uitest::test_profile_two_oleds());
		s.grid().grid().set(4, 3, RGB{10, 20, 30});
		s.grid().grid().set(0, 0, RGB{255, 0, 0});
		s.set_led(7, true);
		s.set_led(9, true);
		s.set_led(9, false);
		s.set_indicator(1, deluge::ui::KnobIndicator{{3, 3, 2, 0}});
		expect(s.state()).to_equal(std::string("indicator lower 3 3 2 0\n"
		                                       "led 7 on\n"
		                                       "pad 0 0 255 0 0\n"
		                                       "pad 4 3 10 20 30\n"));
	});
});
// clang-format on

DELUGE_SPEC(shell_surfaces_spec, shell_surfaces)
