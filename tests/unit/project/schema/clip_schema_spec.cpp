#include "project/schema/clip_schema.h"
#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/write_json.h"
#include "support/printers/schema_report_printer.h"
#include "support/printers/song_document_printer.h"
#include "support/schema/generator.h"
#include "support/schema/round_trip.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <array>
#include <string>
#include <vector>

using namespace deluge::project;
using namespace deluge::project::schema;
using deluge::serialization::parseJson;
using deluge::serialization::Value;
using deluge::serialization::write_json;
namespace sch = deluge::serialization::schema;

namespace {
/// A record holding only a song document's clips, so the clips are tested alone.
inline constexpr std::array kClipsOnlyFields{kClipsField};
inline constexpr sch::RecordDesc kClipsOnlyDesc = sch::closed_record("clipsOnly", kClipsOnlyFields);
inline constexpr sch::Schema<SongDocument> kClipsOnly{&kClipsOnlyDesc};

SongDocument song_with_clips() {
	SongDocument song;
	DocClip& kit = song.clips[4];
	kit.name = "beat";
	kit.loop_length = 384;
	kit.launch_style = LaunchStyle::FILL;
	kit.values.midi_pgm = 3;
	DocNoteRow& row = kit.note_rows.emplace_back();
	row.y = -32768;
	row.bend_range = 12;
	row.drum_id = 7;
	row.sequence_direction = SequenceDirection::OBEY_PARENT;
	song.clip_order.push_back({.id = 4, .in_kit = true});
	DocClip& audio = song.clips[9];
	audio.section = 255;
	DocAudioClip& sample = song.audio_clips[9];
	sample.file_path = "SAMPLES/loop.wav";
	sample.end_pos = 44100;
	sample.reversed = true;
	sample.voice_priority = deluge::plugin::VoicePriority::HIGH;
	song.clip_order.push_back({.id = 9, .audio = true, .arrangement_only = true});
	return song;
}

std::vector<std::string> keys(const Value& map) {
	std::vector<std::string> found;
	for (const auto& [key, value] : map.entries()) {
		found.push_back(key);
	}
	return found;
}
} // namespace

// clang-format off
describe clip_schema("the clips' schema", ${
	it("round-trips instrument and audio clips in their order", _ {
		const SongDocument song = song_with_clips();
		const Value written = sch::encode(kClipsOnly, song);
		const auto back = sch::decode(kClipsOnly, *parseJson(write_json(written)));
		expect(back).to_have_value();
		expect(back->value).to_equal(song);
		expect(write_json(sch::encode(kClipsOnly, back->value))).to_equal(write_json(written));
	});

	it("writes each clip as a record of its kind, its entry's fields first", _ {
		const Value written = sch::encode(kClipsOnly, song_with_clips());
		const Value& kit = written.find("clips")->items()[0];
		const Value& audio = written.find("clips")->items()[1];
		expect(kit.find("kind")->str()).to_equal(std::string{"instrument"});
		expect(audio.find("kind")->str()).to_equal(std::string{"audio"});
		expect(keys(kit)).to_equal(std::vector<std::string>{"kind", "id", "arrangementOnly", "name",
		    "sequenceDirection", "values", "loopLength", "colourOffset", "launchStyle", "section", "inKit", "noteRows"});
		expect(audio.find("filePath")->str()).to_equal(std::string{"SAMPLES/loop.wav"});
		expect(audio.find("voicePriority")->str()).to_equal(std::string{"high"});
	});

	it("refuses two clips with one id", _ {
		const auto back = sch::decode(kClipsOnly, *parseJson(
		    R"({"clips": [{"kind": "instrument", "id": 3}, {"kind": "audio", "id": 3}]})"));
		expect(back.error()).to_equal(sch::LoadError{sch::LoadError::Kind::DUPLICATE_ID, "clips/1"});
	});

	it("keeps a clip's unknown key with the clip", _ {
		const auto back = sch::decode(kClipsOnly, *parseJson(R"({"clips": [{"kind": "instrument", "id": 3, "glow": 1}]})"));
		expect(back->value.clips.at(3).extras.holds("glow")).to_equal(true);
		expect(sch::encode(kClipsOnly, back->value).find("clips")->items()[0].find("glow")->str()).to_equal(std::string{"1"});
	});

	it("round-trips generated clips", _ {
		for (uint32_t seed = 1; seed <= 200; ++seed) {
			const auto checked = sch::test::round_trip(kClipsOnly, sch::test::Generator{seed}.record(kClipsOnlyDesc));
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int clip_schema_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(clip_schema).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
