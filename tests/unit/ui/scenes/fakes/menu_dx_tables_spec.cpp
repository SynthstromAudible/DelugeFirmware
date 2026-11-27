/// @file
/// @brief The DX7 parameter page's own pad layout (`menu/nodes/dx_tables.cpp`): a param's blinking pad is the pad that
///        picks it back.

#include "ui/scenes/menu/nodes/dx_tables.h"

#include "cppspec.hpp"
#include "support/printers/dx_pad_printer.h"

#include <cstdint>
#include <optional>

#include "support/spec_main.h"

namespace dx = deluge::ui::scenes::menu::dx;

namespace {

/// The keyboard scaling params of an operator (its index 8-12), which no pad blinks for.
constexpr int16_t kFirstScaling = 8;
constexpr int16_t kLastScaling = 12;

} // namespace

// clang-format off
describe menu_dx_tables("scenes::menu::dx -- the parameter page's pad layout", ${
	it("picks every operator param back from the pad that blinks for it, and blinks none for keyboard scaling", _ {
		size_t round_trips = 0;
		for (int16_t param = 0; param < dx::kFirstGlobal; param++) {
			const int16_t index = param % dx::kOperatorParams;
			const std::optional<dx::Pad> pad = dx::param_pad(param);
			if (index >= kFirstScaling && index <= kLastScaling) {
				expect(pad).not_().to_have_value();
				continue;
			}
			expect(pad).to_have_value();
			if (pad) {
				expect(dx::pad_param(pad->x, pad->y)).to_equal(std::optional<int16_t>{param});
				round_trips++;
			}
		}
		expect(round_trips).to_equal(size_t{6 * 16});
	});
});
// clang-format on

DELUGE_SPEC(menu_dx_tables_spec, menu_dx_tables)
