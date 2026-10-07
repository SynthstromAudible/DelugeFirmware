/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

// AntialiasedTanh: tanh saturation interpolated against the previous working value, so a jump between samples is
// smoothed rather than aliased. Exact reference values pin the arithmetic on both backends.

#include "dsp-next/fixed/shaping/antialiased_tanh.h"

#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using deluge::dsp::fixed::AntialiasedTanh;

namespace {

/// The outputs of one fresh component at @p drive for the five inputs every case uses, in order.
std::vector<int32_t> run(uint32_t drive) {
	AntialiasedTanh tanh{drive};
	std::vector<int32_t> out;
	for (int32_t in : {0, 1 << 28, -(1 << 28), 1 << 30, 12345678}) {
		out.push_back(tanh.render(in));
	}
	return out;
}

} // namespace

// clang-format off
describe antialiased_tanh("AntialiasedTanh", ${
	it("saturates a sequence at drive 3 exactly", _ {
		expect(run(3)).to_equal(std::vector<int32_t>{0, 55521277, -1, -9, 57900622});
	});
	it("saturates a sequence at drive 5 exactly", _ {
		expect(run(5)).to_equal(std::vector<int32_t>{0, 13880319, -1, -3, 15726067});
	});
	it("saturates a sequence at drive 8 exactly", _ {
		expect(run(8)).to_equal(std::vector<int32_t>{0, 1735039, -1, -1, 2097087});
	});
	it("remembers the last working value", _ {
		AntialiasedTanh tanh{3};
		for (int32_t in : {0, 1 << 28, -(1 << 28), 1 << 30, 12345678}) {
			(void)tanh.render(in);
		}
		expect(tanh.previous()).to_equal(2246249072u);
	});
	it("starts from the value it is given", _ {
		AntialiasedTanh fromZero{5, 0};
		AntialiasedTanh fromSilence{5};
		expect(fromZero.render(1 << 28)).not_().to_equal(fromSilence.render(1 << 28));
	});
	it("renders a block as it renders each sample", _ {
		AntialiasedTanh bySample{5};
		AntialiasedTanh byBlock{5};
		const std::vector<int32_t> in{1 << 20, 1 << 26, -(1 << 27), 1 << 29};
		std::vector<int32_t> out(in.size());
		byBlock.renderBlock(in, out);
		for (size_t i = 0; i < in.size(); i++) {
			expect(out[i]).to_equal(bySample.render(in[i]));
		}
	});
});

CPPSPEC_MAIN(antialiased_tanh);
