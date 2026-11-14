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

// The live histories an input oscillator owns (synth/sources/input/live/live_inputs.h): what each type holds, kept or
// remade on a type change, handed over by swap and freed with the store.

#include "alloc_seams.h"

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/stereo_sample.h"
#include "synth/sources/input/live/live_input.h"
#include "synth/sources/input/live/live_inputs.h"

#include <cppspec.hpp>
#include <optional>
#include <utility>

namespace {
using deluge::synth::InputChannel;
using deluge::synth::LiveInputStore;

/// The channel of the store's mono history, or nothing
std::optional<InputChannel> mono_channel(const LiveInputStore& store) {
	const auto* mono = store.inputs().mono;
	return mono == nullptr ? std::nullopt : std::optional{mono->channel()};
}
} // namespace

// clang-format off
describe live_inputs("deluge::synth::LiveInputStore", ${
	it("holds the left history for the left input", _ {
		const LiveInputStore store = LiveInputStore::for_type(OscType::INPUT_L);
		expect(mono_channel(store) == InputChannel::LEFT).to_be_true();
		expect(store.inputs().stereo == nullptr).to_be_true();
	});

	it("holds the right history for the right input", _ {
		const LiveInputStore store = LiveInputStore::for_type(OscType::INPUT_R);
		expect(mono_channel(store) == InputChannel::RIGHT).to_be_true();
		expect(store.inputs().stereo == nullptr).to_be_true();
	});

	it("holds the stereo history and the left one for the stereo input", _ {
		const LiveInputStore store = LiveInputStore::for_type(OscType::INPUT_STEREO);
		expect(store.inputs().stereo != nullptr).to_be_true();
		expect(store.inputs().stereo->channel() == InputChannel::STEREO).to_be_true();
		expect(mono_channel(store) == InputChannel::LEFT).to_be_true();
	});

	it("holds nothing for a type that is not an input", _ {
		for (const OscType type : {OscType::SAW, OscType::SAMPLE, OscType::DX7}) {
			const LiveInputStore store = LiveInputStore::for_type(type);
			expect(store.inputs().mono == nullptr && store.inputs().stereo == nullptr).to_be_true();
		}
	});

	it("knows whether it already holds what a type reads", _ {
		const LiveInputStore left = LiveInputStore::for_type(OscType::INPUT_L);
		expect(left.holds_for(OscType::INPUT_L)).to_be_true();
		expect(left.holds_for(OscType::INPUT_R)).to_be_false();
		expect(left.holds_for(OscType::INPUT_STEREO)).to_be_false();
		expect(left.holds_for(OscType::SAW)).to_be_false();
		const LiveInputStore none;
		expect(none.holds_for(OscType::SAW)).to_be_true();
		expect(none.holds_for(OscType::INPUT_L)).to_be_false();
		const LiveInputStore stereo = LiveInputStore::for_type(OscType::INPUT_STEREO);
		expect(stereo.holds_for(OscType::INPUT_STEREO)).to_be_true();
		expect(stereo.holds_for(OscType::INPUT_L)).to_be_false();
	});

	it("hands its histories over by swap", _ {
		LiveInputStore held = LiveInputStore::for_type(OscType::INPUT_L);
		const auto* left = held.inputs().mono;
		LiveInputStore next = LiveInputStore::for_type(OscType::INPUT_R);
		const auto* right = next.inputs().mono;
		held.swap(next);
		expect(held.inputs().mono == right).to_be_true();
		expect(next.inputs().mono == left).to_be_true();
	});

	it("gives each frame its history", _ {
		const LiveInputStore store = LiveInputStore::for_type(OscType::INPUT_STEREO);
		expect(store.inputs().of<deluge::dsp::Q<31>>() == store.inputs().mono).to_be_true();
		expect(store.inputs().of<deluge::dsp::StereoSample>() == store.inputs().stereo).to_be_true();
	});

	it("moves its histories, leaving none behind", _ {
		LiveInputStore from = LiveInputStore::for_type(OscType::INPUT_L);
		const auto* left = from.inputs().mono;
		const LiveInputStore to = std::move(from);
		expect(to.inputs().mono == left).to_be_true();
		expect(from.inputs().mono == nullptr).to_be_true(); // NOLINT(bugprone-use-after-move): moved-from is empty
	});
});
// clang-format on

CPPSPEC_MAIN(live_inputs);
