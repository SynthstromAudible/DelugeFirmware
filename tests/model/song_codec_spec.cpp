#include "codec_support.h"
#include "deluge-project/song_codec.h"
#include "serialization/parse_json.h"
#include "serialization/write_json.h"

#include <cppspec.hpp>

using namespace deluge::project;
using deluge::project::test::both_shapes;
using deluge::serialization::Value;

namespace {
/// encode, wrap the Map as a song, write it as JSON and parse it back: what decode sees after a save.
Value as_song(const Value& encoded) {
	auto root = Value::makeMap();
	root.set("song", encoded);
	return *deluge::serialization::parseJson(deluge::serialization::write_json(root))->find("song");
}
} // namespace

// clang-format off
describe song_codec("the song-level records", ${
	it("round-trips the transport, reading the tick fraction as unsigned", _ {
		Transport t;
		t.samples_per_timer_tick_big = (uint64_t{12} << 32) | 0xFFFFFFFFu;
		t.inside_world_tick_magnitude = 2;
		Transport back;
		expect(decode(as_song(encode(t)), back).has_value()).to_equal(true);
		expect(back == t).to_equal(true);
		both_shapes(R"(<song timePerTimerTick="12" timerTickFraction="-1" inputTickMagnitude="2"/>)",
		            R"({"song": {"timePerTimerTick": 12, "timerTickFraction": -1, "inputTickMagnitude": 2}})",
		            [&](const Value& song) {
			Transport read;
			(void)decode(song, read);
			expect(read == t).to_equal(true);
		});
	});

	it("round-trips the groove at any tick magnitude", _ {
		Groove g{.swing_amount = -20, .swing_interval = deluge::engine::sync_level_from_index(6)};
		Groove back;
		DecodeContext relative{.input_tick_magnitude = 3, .swing_interval_is_relative = true};
		expect(decode(as_song(encode(g, 3)), back, relative).has_value()).to_equal(true);
		expect(back == g).to_equal(true);
	});

	it("reads an old file swing interval as internal, a new one as relative", _ {
		both_shapes(R"(<song swingAmount="10" swingInterval="5"/>)",
		            R"({"song": {"swingAmount": 10, "swingInterval": 5}})", [&](const Value& song) {
			Groove relative;
			Groove old;
			(void)decode(song, relative, {.input_tick_magnitude = 3, .swing_interval_is_relative = true});
			(void)decode(song, old, {.input_tick_magnitude = 3, .swing_interval_is_relative = false});
			expect(deluge::engine::index_of(relative.swing_interval)).to_equal(3);
			expect(deluge::engine::index_of(old.swing_interval)).to_equal(5);
		});
	});

	it("clamps the swing amount after narrowing it, as the reader does", _ {
		both_shapes(R"(<song swingAmount="80"/>)", R"({"song": {"swingAmount": 80}})", [&](const Value& song) {
			Groove g;
			(void)decode(song, g, {});
			expect(static_cast<int>(g.swing_amount)).to_equal(49);
		});
		both_shapes(R"(<song swingAmount="200"/>)", R"({"song": {"swingAmount": 200}})", [&](const Value& song) {
			Groove g;
			(void)decode(song, g, {});
			expect(static_cast<int>(g.swing_amount)).to_equal(-49); // 200 narrows to -56
		});
	});

	it("keeps a value the file lacks", _ {
		both_shapes(R"(<song/>)", R"({"song": {}})", [&](const Value& song) {
			Groove g{.swing_amount = 7, .swing_interval = deluge::engine::sync_level_from_index(4)};
			Groove before = g;
			(void)decode(song, g, {});
			expect(g == before).to_equal(true);
			ScaleSettings s;
			s.key.rootNote = 5;
			ScaleSettings sBefore = s;
			(void)decode(song, s);
			expect(s == sBefore).to_equal(true);
		});
	});

	it("round-trips the scale, and accepts mode notes out of order as addUntrusted does", _ {
		ScaleSettings s;
		s.key.rootNote = 9;
		s.key.modeNotes = deluge::music::NoteSet{0, 2, 3, 5, 7, 8, 10};
		s.user_scale_notes = deluge::music::NoteSet{0, 4, 7};
		s.disabled_preset_scales.set(2);
		ScaleSettings back;
		expect(decode(as_song(encode(s)), back).has_value()).to_equal(true);
		expect(back == s).to_equal(true);
		both_shapes(R"(<song><modeNotes><modeNote>0</modeNote><modeNote>4</modeNote><modeNote>2</modeNote></modeNotes></song>)",
		            R"({"song": {"modeNotes": [{"modeNote": 0}, {"modeNote": 4}, {"modeNote": 2}]}})",
		            [&](const Value& song) {
			ScaleSettings read;
			(void)decode(song, read);
			expect(read.key.modeNotes == deluge::music::NoteSet{0, 4, 5}).to_equal(true);
		});
	});

	it("round-trips the reverb through one rounding, fixing the old width", _ {
		ReverbSettings r{.room_size = 0.3f, .hpf = 0.1f, .lpf = 0.9f, .damping = 0.5f, .width = 1.0f, .pan = -12,
		                 .sidechain_volume = 5, .sidechain_shape = 6, .sidechain_attack = 7, .sidechain_release = 8,
		                 .sidechain_sync = deluge::engine::sync_level_from_index(7)};
		DecodeContext at2{.input_tick_magnitude = 2};
		ReverbSettings once;
		(void)decode(as_song(encode(r, 2)), once, at2);
		ReverbSettings twice;
		(void)decode(as_song(encode(once, 2)), twice, at2);
		expect(twice == once).to_equal(true);
		expect(deluge::engine::index_of(once.sidechain_sync)).to_equal(7);
		both_shapes(R"(<song width="-2147483648"/>)", R"({"song": {"width": -2147483648}})", [&](const Value& reverb) {
			ReverbSettings read;
			(void)decode(reverb, read, {});
			expect(read.width).to_equal(1.0f);
		});
	});
});

CPPSPEC_SPEC(song_codec)
