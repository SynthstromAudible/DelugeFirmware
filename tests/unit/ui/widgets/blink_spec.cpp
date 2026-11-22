#include "cppspec.hpp"
#include "support/spec_main.h"
#include "ui/widgets/core/blink.h"
#include "ui/widgets/core/metrics.h"

using namespace deluge::ui;

// clang-format off
describe cursor_blink("ui::Blink", $ {
	it("is unarmed until a restart, whatever its period", _ {
		widgets::core::Blink b{};
		expect(b.armed()).to_be_false();
		b.restart_dark(100);
		expect(b.armed()).to_be_true();
		b = widgets::core::Blink{};
		expect(b.armed()).to_be_false();
	});

	it("restarted dark, is dark for one period and then lit", _ {
		widgets::core::Blink b{.period_ms = static_cast<uint16_t>(widgets::core::kFlashTime)};
		b.restart_dark(1000);
		expect(blink_lit(b, 1000)).to_be_false();
		expect(blink_lit(b, 1109)).to_be_false();
		expect(blink_lit(b, 1110)).to_be_true();
		expect(blink_next_flip_in(b, 1000)).to_equal(uint32_t(110));
		expect(blink_next_flip_in(b, 1150)).to_equal(uint32_t(70));
	});

	it("restarted lit, is lit for one period and then dark", _ {
		widgets::core::Blink b{.period_ms = static_cast<uint16_t>(widgets::core::kFlashTime)};
		b.restart_lit(1000);
		expect(blink_lit(b, 1000)).to_be_true();
		expect(blink_lit(b, 1109)).to_be_true();
		expect(blink_lit(b, 1110)).to_be_false();
		expect(blink_next_flip_in(b, 1000)).to_equal(uint32_t(110));
	});
});

DELUGE_SPEC(blink_spec, cursor_blink)
