/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "instruments/deluge/cable_records.h"
#include "synth/modulation/patch_routing.h"

#include "modulation/params/param.h"

#include <cppspec.hpp>
#include <utility>
#include <vector>

using namespace deluge::synth;
using deluge::instrument::CableRecords;
namespace params = deluge::modulation::params;

namespace {
constexpr uint8_t kCutoff = params::LOCAL_LPF_FREQ;
constexpr uint8_t kPitch = params::LOCAL_PITCH_ADJUST;
constexpr uint8_t kGlobalVolume = params::GLOBAL_VOLUME_POST_FX;

CableRecords records_of(std::initializer_list<CableRecord> list) {
	CableRecords r;
	for (const CableRecord& c : list) {
		r.append(c);
	}
	return r;
}

std::bitset<kMaxCables> all_usable(const CableRecords& r) {
	return usability_of(r.records(), [](PatchSource, CableTarget) { return true; });
}

std::vector<uint8_t> order_of(const RoutingBuild& b) {
	return {b.order.begin(), b.order.begin() + b.num_cables};
}
} // namespace

// clang-format off
describe patch_routing("the synth library's cable routing", ${
	it("encodes targets as the runtime's descriptor word", _ {
		expect(CableTarget::param(kCutoff).word).to_equal(uint32_t{kCutoff} | 0xFFFFFF00u);
		const CableTarget depth = CableTarget::depth_of(kCutoff, PatchSource::LFO_LOCAL_1);
		expect(depth.is_param()).to_equal(false);
		expect(depth.bottom_source()).to_equal(PatchSource::LFO_LOCAL_1);
		expect(depth.modulated()).to_equal(CableTarget::param(kCutoff));
		expect(CableTarget{}.is_null()).to_equal(true);
	});

	it("moves unusable cables behind the usable ones", _ {
		const CableRecords r = records_of({
		    {PatchSource::ENVELOPE_0, CableTarget::param(kCutoff), CablePolarity::BIPOLAR},
		    {PatchSource::VELOCITY, CableTarget::param(kPitch), CablePolarity::BIPOLAR},
		    {PatchSource::LFO_LOCAL_1, CableTarget::param(kCutoff), CablePolarity::BIPOLAR},
		});
		const auto usable = usability_of(r.records(), [](PatchSource s, CableTarget) { return s != PatchSource::ENVELOPE_0; });
		RoutingBuild b;
		compile_routing(r.records(), usable, b);
		expect(int32_t{b.num_usable}).to_equal(2);
		expect(order_of(b)).to_equal(std::vector<uint8_t>{2, 1, 0});
	});

	it("drops a depth cable whose modulated cable is missing", _ {
		const CableRecords r = records_of({
		    {PatchSource::X, CableTarget::depth_of(kCutoff, PatchSource::LFO_LOCAL_1), CablePolarity::BIPOLAR},
		    {PatchSource::ENVELOPE_0, CableTarget::param(kCutoff), CablePolarity::BIPOLAR},
		});
		RoutingBuild b;
		compile_routing(r.records(), all_usable(r), b);
		expect(int32_t{b.num_usable}).to_equal(1);
		expect(order_of(b)).to_equal(std::vector<uint8_t>{1, 0});
	});

	it("groups each destination's cables and sorts the destinations by their word", _ {
		const CableRecords r = records_of({
		    {PatchSource::ENVELOPE_0, CableTarget::param(kPitch), CablePolarity::BIPOLAR},
		    {PatchSource::LFO_LOCAL_1, CableTarget::param(kCutoff), CablePolarity::BIPOLAR},
		    {PatchSource::VELOCITY, CableTarget::param(kPitch), CablePolarity::BIPOLAR},
		    {PatchSource::LFO_GLOBAL_1, CableTarget::param(kGlobalVolume), CablePolarity::BIPOLAR},
		});
		RoutingBuild b;
		compile_routing(r.records(), all_usable(r), b);
		expect(order_of(b)).to_equal(std::vector<uint8_t>{0, 2, 1, 3});
		expect(int32_t{b.num_destinations[0]}).to_equal(2);
		expect(b.destinations[0][0].target.word < b.destinations[0][1].target.word).to_equal(true);
		expect(b.destinations[0][2].sources).to_equal(0u);
		expect(int32_t{b.num_destinations[1]}).to_equal(1);
		expect(b.sources_patched[1]).to_equal(1u << std::to_underlying(PatchSource::LFO_GLOBAL_1));
	});

	it("links a depth cable's range value to the cable it modulates", _ {
		const CableRecords r = records_of({
		    {PatchSource::ENVELOPE_0, CableTarget::param(kCutoff), CablePolarity::BIPOLAR},
		    {PatchSource::X, CableTarget::depth_of(kCutoff, PatchSource::ENVELOPE_0), CablePolarity::BIPOLAR},
		});
		RoutingBuild b;
		compile_routing(r.records(), all_usable(r), b);
		// The depth destination sorts first and takes range value 0; the modulated cable reads it.
		const RoutedDestination& range = b.destinations[0][0];
		expect(range.target).to_equal(CableTarget::depth_of(kCutoff, PatchSource::ENVELOPE_0));
		const uint8_t modulated = b.destinations[0][1].first_cable;
		expect(int32_t{b.cables[modulated].range_index}).to_equal(0);
		expect((b.destinations[0][1].sources >> std::to_underlying(PatchSource::X)) & 1u).to_equal(1u);
		expect((range.sources >> std::to_underlying(PatchSource::ENVELOPE_0)) & 1u).to_equal(1u);
		expect(b.missing_range_link).to_equal(false);
	});

	it("is idempotent on its own order", _ {
		CableRecords r = records_of({
		    {PatchSource::ENVELOPE_0, CableTarget::param(kPitch), CablePolarity::BIPOLAR},
		    {PatchSource::LFO_LOCAL_1, CableTarget::param(kCutoff), CablePolarity::UNIPOLAR},
		    {PatchSource::VELOCITY, CableTarget::param(kPitch), CablePolarity::BIPOLAR},
		});
		RoutingBuild b;
		compile_routing(r.records(), all_usable(r), b);
		r.permute(std::span{b.order}.first(b.num_cables));
		compile_routing(r.records(), all_usable(r), b);
		expect(order_of(b)).to_equal(std::vector<uint8_t>{0, 1, 2});
	});

	it("removes a cable by moving the last one into its place", _ {
		CableRecords r = records_of({
		    {PatchSource::ENVELOPE_0, CableTarget::param(kPitch), CablePolarity::BIPOLAR},
		    {PatchSource::LFO_LOCAL_1, CableTarget::param(kCutoff), CablePolarity::BIPOLAR},
		    {PatchSource::VELOCITY, CableTarget::param(kPitch), CablePolarity::BIPOLAR},
		});
		r.erase_by_moving_last(0);
		expect(r.size()).to_equal(size_t{2});
		expect(r[0].source).to_equal(PatchSource::VELOCITY);
		expect(r.find(PatchSource::LFO_LOCAL_1, CableTarget::param(kCutoff)).value_or(99)).to_equal(size_t{1});
	});
});
// clang-format on

CPPSPEC_MAIN(patch_routing);
