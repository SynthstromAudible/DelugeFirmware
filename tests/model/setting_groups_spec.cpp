#include "deluge-project/arp_codec.h"
#include "deluge-project/clip_values.h"
#include "deluge-project/setting_group.h"
#include "deluge-project/song.h"
#include "deluge-project/structure_codec.h"

#include "cppspec.hpp"

#include <string>
#include <vector>

using namespace deluge::project;
using deluge::engine::SettingInfo;
using deluge::engine::SettingKind;
using deluge::serialization::Value;

namespace {

/// @brief Every legal value of @p row, as text: each choice, both bools, or an integer row's least, greatest and
///        middle value; one name for a text row.
std::vector<std::string> legal_values(const SettingInfo& row) {
	switch (row.kind) {
	case SettingKind::CHOICE: {
		std::vector<std::string> out;
		for (std::string_view choice : row.choices) {
			out.emplace_back(choice);
		}
		return out;
	}
	case SettingKind::BOOL:
		return {"0", "1"};
	case SettingKind::INTEGER:
		if (!row.user.identity()) {
			// What a menu writes: its user range's ends and middle, through the row's mapping.
			const int32_t mid = row.user.user_min + (row.user.user_max - row.user.user_min) / 2;
			return {std::to_string(*deluge::engine::to_stored(row, row.user.user_min)),
			        std::to_string(*deluge::engine::to_stored(row, row.user.user_max)),
			        std::to_string(*deluge::engine::to_stored(row, mid))};
		}
		return {std::to_string(row.min), std::to_string(row.max),
		        std::to_string(row.min + (int64_t{row.max} - row.min) / 2)};
	case SettingKind::TEXT:
		return {"NAME1"};
	case SettingKind::STRUCTURED:
		break;
	}
	return {};
}

/// @brief Write and read back every legal value of every path of @p group at @p owner; the first mismatch, or empty.
std::string round_trip(Song& song, SettingGroupId group, const SettingOwner& owner) {
	const SettingAddress address{group, owner};
	for (const SettingInfo& row : table_of(group)) {
		if (row.path == "arpeggiator/preset") {
			continue; // derived from the arp's fields: "custom" reads back as the preset they make
		}
		for (const std::string& value : legal_values(row)) {
			if (!write_setting(song, address, row.path, Value::makeScalar(value))) {
				return std::string{row.path} + "=" + value + " refused";
			}
			const std::optional<Value> back = read_setting(song, address, row.path);
			if (!back || back->str() != value) {
				return std::string{row.path} + "=" + value + " read back " + (back ? back->str() : "nothing");
			}
		}
	}
	return {};
}

} // namespace

// clang-format off
describe setting_groups_song("the song settings group", $ {
	it("round-trips every legal value of every path through the record", _ {
		Song song;
		expect(round_trip(song, SettingGroupId::SONG, SongAddress{})).to_equal(std::string{});
	});

	it("refuses a value outside its row, or a path no row holds, and changes nothing", _ {
		Song song;
		const SettingAddress address{SettingGroupId::SONG, SongAddress{}};
		const ReverbSettings before = song.reverb;
		expect(write_setting(song, address, "reverb/model", Value::makeScalar("9"))).to_be_false();
		expect(write_setting(song, address, "no/such/path", Value::makeScalar("1"))).to_be_false();
		expect(song.reverb == before).to_be_true();
	});

	it("keeps the reverb model in the record", _ {
		Song song;
		const SettingAddress address{SettingGroupId::SONG, SongAddress{}};
		expect(write_setting(song, address, "reverb/model", Value::makeScalar("1"))).to_be_true();
		expect(static_cast<int>(song.reverb.model)).to_equal(1);
	});

	it("disables one preset scale per path, leaving the others", _ {
		Song song;
		const SettingAddress address{SettingGroupId::SONG, SongAddress{}};
		expect(write_setting(song, address, "scales/disabled/3", Value::makeScalar("1"))).to_be_true();
		expect(song.scale.disabled_preset_scales.test(3)).to_be_true();
		expect(song.scale.disabled_preset_scales.count()).to_equal(size_t{1});
	});
});

describe setting_groups_owners("the owner setting groups", $ {
	it("refuses an owner that does not resolve, reading and writing", _ {
		Song song;
		const SettingAddress missing{SettingGroupId::TRACK, TrackAddress{.output_id = 999}};
		expect(write_setting(song, missing, "name", Value::makeScalar("X"))).to_be_false();
		expect(read_setting(song, missing, "name").has_value()).to_be_false();
	});

	it("round-trips the track and instrument-device groups through an output's record", _ {
		Song song;
		(void)song.output_structure.getOrCreate(7);
		expect(round_trip(song, SettingGroupId::TRACK, TrackAddress{.output_id = 7})).to_equal(std::string{});
		expect(round_trip(song, SettingGroupId::INSTRUMENT_DEVICE, TrackAddress{.output_id = 7})).to_equal(std::string{});
		expect(round_trip(song, SettingGroupId::AUDIO_INPUT, TrackAddress{.output_id = 7})).to_equal(std::string{});
	});

	it("round-trips the lane group through a drum's record", _ {
		Song song;
		(void)song.drum_structure.getOrCreate(5);
		expect(round_trip(song, SettingGroupId::LANE, LaneAddress{.kit_id = 7, .drum_id = 5})).to_equal(std::string{});
	});

	it("round-trips the note-row group through a melodic row and a kit row", _ {
		Song song;
		DocClip& clip = song.clip_structure.getOrCreate(11);
		clip.note_rows.emplace_back().y = 60;
		clip.note_rows.emplace_back().drum_id = 5;
		expect(round_trip(song, SettingGroupId::NOTE_ROW, NoteRowAddress{.clip_id = 11, .is_kit = false, .row_key = 60}))
		    .to_equal(std::string{});
		expect(round_trip(song, SettingGroupId::NOTE_ROW, NoteRowAddress{.clip_id = 11, .is_kit = true, .row_key = 5}))
		    .to_equal(std::string{});
	});

	it("round-trips the clip and clip-value groups through a clip's record", _ {
		Song song;
		(void)song.clip_structure.getOrCreate(7);
		expect(round_trip(song, SettingGroupId::CLIP, ClipAddress{.clip_id = 7})).to_equal(std::string{});
		expect(round_trip(song, SettingGroupId::CLIP_VALUES, ClipAddress{.clip_id = 7})).to_equal(std::string{});
	});

	it("resolves a clip value over the track's, and the track's when the clip has none", _ {
		ClipValues values;
		expect(resolve(values, "midiPGM", 128)).to_equal(128);
		values.midi_pgm = 5;
		expect(resolve(values, "midiPGM", 128)).to_equal(5);
	});

	it("encodes a clip's name, direction and values and decodes them back", _ {
		DocClip clip;
		clip.name = "VERSE";
		clip.sequence_direction = SequenceDirection::PINGPONG;
		clip.values.midi_bank = 3;
		clip.values.bend_range = 12;
		DocClip back;
		expect(decode(encode(clip), back).has_value()).to_be_true();
		expect(back.name).to_equal(std::string{"VERSE"});
		expect(back.sequence_direction == SequenceDirection::PINGPONG).to_be_true();
		expect(back.values == clip.values).to_be_true();
	});

	it("round-trips a clip's arp and a kit row's arp through their records", _ {
		Song song;
		(void)song.clip_structure.getOrCreate(7);
		(void)song.drum_structure.getOrCreate(5);
		expect(round_trip(song, SettingGroupId::CLIP_ARP, ClipAddress{.clip_id = 7})).to_equal(std::string{});
		expect(round_trip(song, SettingGroupId::LANE_ARP, LaneAddress{.kit_id = 3, .drum_id = 5})).to_equal(std::string{});
	});

	it("writes an arp preset as its three fields and reads the preset back from them", _ {
		Song song;
		(void)song.clip_structure.getOrCreate(7);
		const SettingAddress arp{SettingGroupId::CLIP_ARP, ClipAddress{.clip_id = 7}};
		expect(read_setting(song, arp, "arpeggiator/preset")->str()).to_equal(std::string{"off"});
		expect(write_setting(song, arp, "arpeggiator/preset", Value::makeScalar("down"))).to_be_true();
		expect(read_setting(song, arp, "arpeggiator/arpMode")->str()).to_equal(std::string{"arp"});
		expect(read_setting(song, arp, "arpeggiator/octaveMode")->str()).to_equal(std::string{"down"});
		expect(read_setting(song, arp, "arpeggiator/noteMode")->str()).to_equal(std::string{"down"});
		expect(read_setting(song, arp, "arpeggiator/preset")->str()).to_equal(std::string{"down"});
		expect(write_setting(song, arp, "arpeggiator/noteMode", Value::makeScalar("random"))).to_be_true();
		expect(read_setting(song, arp, "arpeggiator/preset")->str()).to_equal(std::string{"custom"});
		expect(write_setting(song, arp, "arpeggiator/preset", Value::makeScalar("off"))).to_be_true();
		expect(read_setting(song, arp, "arpeggiator/arpMode")->str()).to_equal(std::string{"off"});
		expect(read_setting(song, arp, "arpeggiator/noteMode")->str()).to_equal(std::string{"random"});
	});

	it("chooses the custom preset as an arp going up, as the preset menu does", _ {
		Song song;
		(void)song.clip_structure.getOrCreate(7);
		const SettingAddress arp{SettingGroupId::CLIP_ARP, ClipAddress{.clip_id = 7}};
		expect(write_setting(song, arp, "arpeggiator/noteMode", Value::makeScalar("walk3"))).to_be_true();
		expect(write_setting(song, arp, "arpeggiator/preset", Value::makeScalar("custom"))).to_be_true();
		expect(read_setting(song, arp, "arpeggiator/arpMode")->str()).to_equal(std::string{"arp"});
		expect(read_setting(song, arp, "arpeggiator/noteMode")->str()).to_equal(std::string{"up"});
	});

	it("reads a host-only arp value the record does not hold as its default", _ {
		Song song;
		(void)song.clip_structure.getOrCreate(7);
		const SettingAddress arp{SettingGroupId::CLIP_ARP, ClipAddress{.clip_id = 7}};
		expect(read_setting(song, arp, "arpeggiator/noteProbability")->str()).to_equal(std::string{"-1"});
		expect(holds_setting(song, arp, "arpeggiator/rate")).to_be_false();
		expect(write_setting(song, arp, "arpeggiator/rate", Value::makeScalar("12"))).to_be_true();
		expect(holds_setting(song, arp, "arpeggiator/rate")).to_be_true();
		expect(song.clip_structure.find(7)->values.arp->rate).to_equal(std::optional<int32_t>{12});
	});

	it("encodes an arp's record and decodes it back, absolute and relative to a tick magnitude", _ {
		ArpRecord record;
		record.mode = 1;
		record.octave_mode = 3;
		record.note_mode = 8;
		record.mpe_velocity = 2;
		record.num_octaves = 4;
		record.step_repeat = 3;
		record.chord_type = 5;
		record.sync_level = 6;
		record.sync_type = 10;
		record.randomizer_lock = true;
		record.kit_arp = false;
		record.rate = -5;
		record.note_probability = -1;
		record.spread_octave = 1234;
		for (const std::optional<int32_t> magnitude : {std::optional<int32_t>{}, std::optional<int32_t>{2}}) {
			ArpRecord back;
			decode(encode(record, magnitude), back, magnitude);
			expect(back == record).to_be_true();
		}
		expect(encode(record, 2).find("syncLevel")->str()).to_equal(std::string{"7"});
		expect(encode(record, std::nullopt).find("syncLevel")->str()).to_equal(std::string{"6"});
	});

	it("decodes an arp's mode from the single mode word of older files", _ {
		Value element = Value::makeMap();
		element.set("mode", Value::makeScalar("both"));
		ArpRecord back;
		decode(element, back, std::nullopt);
		expect(static_cast<int>(back.mode)).to_equal(1);
		expect(static_cast<int>(back.octave_mode)).to_equal(3);
		expect(static_cast<int>(back.note_mode)).to_equal(0);
	});

	it("reads a clip's and a sound kit row's arp from the document", _ {
		DocClip clip;
		Value clipElement = Value::makeMap();
		Value arp = Value::makeMap();
		arp.set("arpMode", Value::makeScalar("arp"));
		arp.set("numOctaves", Value::makeScalar("3"));
		clipElement.set("arpeggiator", arp);
		expect(decode(clipElement, clip).has_value()).to_be_true();
		expect(clip.values.arp.has_value()).to_be_true();
		expect(static_cast<int>(clip.values.arp->num_octaves)).to_equal(3);
	});

	it("round-trips the audio clip group through a clip's descriptor", _ {
		Song song;
		(void)song.audio_clip_structure.getOrCreate(12);
		expect(round_trip(song, SettingGroupId::AUDIO_CLIP, ClipAddress{.clip_id = 12})).to_equal(std::string{});
	});
});
// clang-format on

int setting_groups_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(setting_groups_song, setting_groups_owners).exec().is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
