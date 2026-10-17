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

#include "deluge-project/mapping_table.h"
#include "deluge-project/song.h"

#include <cppspec.hpp>

using namespace deluge::project;

namespace {
MidiCommand cc(uint8_t channel, uint8_t number) {
	return MidiCommand{.channel_or_zone = channel, .note_or_cc = number};
}
ParamTarget out_param(uint32_t output, uint32_t param) {
	return ParamTarget{{ControlsAddress::Kind::OUTPUT, output}, deluge::engine::ParamId{param}};
}
Mapping knob(MidiCommand source, ParamTarget target) {
	return Mapping{.source = source, .target = target};
}
} // namespace

// clang-format off
describe mapping_table("the song's mapping table", ${
	it("replaces a knob learned again on the same source and param", _ {
		MappingTable table;
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		expect(table.entries().size()).to_equal(size_t{1});
	});

	it("keeps two sources on one param until the param is unlearned", _ {
		MappingTable table;
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		table.learn(knob(cc(1, 6), out_param(1, 2)));
		table.learn(knob(cc(1, 5), out_param(1, 3)));
		expect(table.entries().size()).to_equal(size_t{3});
		expect(table.find(out_param(1, 2))->source).to_equal(cc(1, 5));
		expect(table.unlearn(out_param(1, 2))).to_equal(size_t{2});
		expect(table.entries().size()).to_equal(size_t{1});
		expect(table.find(out_param(1, 2))).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("tells devices apart as part of the source", _ {
		MappingTable table;
		MidiCommand other = cc(1, 5);
		other.device = MidiDeviceRef{.port = MidiDeviceRef::Port::UPSTREAM_USB_1};
		table.learn(knob(cc(1, 5), out_param(1, 2)));
		table.learn(knob(other, out_param(1, 2)));
		expect(table.entries().size()).to_equal(size_t{2});
	});

	it("holds one source per clip mute, the latest", _ {
		MappingTable table;
		table.learn(Mapping{.source = cc(1, 5), .target = ClipMuteTarget{7}});
		table.learn(Mapping{.source = cc(2, 9), .target = ClipMuteTarget{7}});
		expect(table.entries().size()).to_equal(size_t{1});
		expect(table.find(ClipMuteTarget{7})->source).to_equal(cc(2, 9));
		expect(table.find(ClipMuteTarget{8})).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("calls a CC knob relative and a pitch bend not", _ {
		MappingTable table;
		expect(table.learn(knob(cc(0, 5), out_param(1, 2))).relative).to_be_true();
		expect(table.learn(knob(cc(0, 128), out_param(1, 3))).relative).to_be_false();
	});

	it("moves an output's params and input, dropping the target's own", _ {
		MappingTable table;
		table.learn(knob(cc(0, 1), out_param(1, 2)));
		table.learn(Mapping{.source = cc(0, 2), .target = TrackInputTarget{1}});
		table.learn(knob(cc(0, 3), out_param(2, 9)));
		table.learn(Mapping{.source = cc(0, 4), .target = TrackInputTarget{2}});
		table.learn(knob(cc(0, 5), out_param(3, 9)));
		table.retarget_output(1, 2);
		expect(table.entries().size()).to_equal(size_t{3});
		expect(table.find(out_param(2, 2))->source).to_equal(cc(0, 1));
		expect(table.find(TrackInputTarget{2})->source).to_equal(cc(0, 2));
		expect(table.find(out_param(2, 9))).to_equal(static_cast<const Mapping*>(nullptr));
		expect(table.find(out_param(3, 9))->source).to_equal(cc(0, 5));
		expect(table.find(out_param(1, 2))).to_equal(static_cast<const Mapping*>(nullptr));
	});

	it("copies an output's params and a clip's mute", _ {
		MappingTable table;
		table.learn(knob(cc(0, 1), out_param(1, 2)));
		table.learn(knob(cc(0, 2), out_param(1, 3)));
		table.learn(Mapping{.source = cc(0, 3), .target = TrackInputTarget{1}});
		table.learn(Mapping{.source = cc(0, 4), .target = ClipMuteTarget{5}});
		table.copy_params(1, 4);
		table.copy_clip_mute(5, 6);
		expect(table.entries().size()).to_equal(size_t{7});
		expect(table.find(out_param(4, 3))->source).to_equal(cc(0, 2));
		expect(table.find(TrackInputTarget{4})).to_equal(static_cast<const Mapping*>(nullptr));
		expect(table.find(ClipMuteTarget{6})->source).to_equal(cc(0, 4));
		expect(table.find(ClipMuteTarget{5})->source).to_equal(cc(0, 4));
	});

	it("forgets only the entries of what was deleted", _ {
		MappingTable table;
		table.learn(Mapping{.source = cc(0, 1), .target = ClipMuteTarget{1}});
		table.learn(Mapping{.source = cc(0, 2), .target = DrumMuteTarget{1}});
		table.learn(Mapping{.source = cc(0, 3), .target = LaneInputTarget{1}});
		table.learn(knob(cc(0, 4), ParamTarget{{ControlsAddress::Kind::DRUM, 1}, deluge::engine::ParamId{1}}));
		table.learn(knob(cc(0, 5), out_param(1, 1)));
		table.learn(Mapping{.source = cc(0, 6), .target = TrackInputTarget{1}});
		table.learn(Mapping{.source = cc(0, 7), .target = SectionLaunchTarget{1}});
		table.forget_clip(1);
		expect(table.entries().size()).to_equal(size_t{6});
		table.forget_drum(1);
		expect(table.entries().size()).to_equal(size_t{3});
		table.forget_output(1);
		expect(table.entries().size()).to_equal(size_t{1});
		expect(table.find(SectionLaunchTarget{1})->source).to_equal(cc(0, 7));
	});

	it("compares equal by entries, and lives on the song", _ {
		MappingTable a;
		MappingTable b;
		expect(a).to_equal(b);
		a.learn(knob(cc(0, 1), out_param(1, 2)));
		expect(a).not_().to_equal(b);
		b.learn(knob(cc(0, 1), out_param(1, 2)));
		expect(a).to_equal(b);
		Song song;
		song.mappings = a;
		expect(song.mappings).to_equal(b);
	});
});

CPPSPEC_SPEC(mapping_table)
