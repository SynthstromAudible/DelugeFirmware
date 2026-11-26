/// @file
/// @brief The DX7 algorithm diagrams (`menu/nodes/dx_algorithms.cpp`): one bitmap per algorithm, each the size its
///        geometry names, and an algorithm past the last drawn as the last.

#include "ui/scenes/menu/nodes/dx_algorithms.h"

#include "cppspec.hpp"

#include <cstddef>

#include "support/spec_main.h"

using deluge::ui::scenes::menu::dx_algorithm_diagram;
using deluge::ui::scenes::menu::DxAlgorithmDiagram;
using deluge::ui::scenes::menu::kDxAlgorithms;

// clang-format off
describe menu_dx_algorithms("scenes::menu -- the DX7 algorithm diagrams", ${
	it("holds a bitmap of width by bytes-tall bytes for every algorithm, tall enough for its height", _ {
		for (size_t algorithm = 0; algorithm < kDxAlgorithms; algorithm++) {
			const DxAlgorithmDiagram& d = dx_algorithm_diagram(algorithm);
			expect(d.data.size()).to_equal(size_t{d.width} * d.num_bytes_tall);
			expect(size_t{d.num_bytes_tall} * 8).not_().to_be_less_than(size_t{d.height});
			expect(d.width).to_be_greater_than(uint8_t{0});
		}
	});

	it("draws an algorithm past the last as the last", _ {
		expect(&dx_algorithm_diagram(kDxAlgorithms)).to_equal(&dx_algorithm_diagram(kDxAlgorithms - 1));
		expect(&dx_algorithm_diagram(0)).not_().to_equal(&dx_algorithm_diagram(kDxAlgorithms - 1));
	});
});
// clang-format on

DELUGE_SPEC(menu_dx_algorithms_spec, menu_dx_algorithms)
