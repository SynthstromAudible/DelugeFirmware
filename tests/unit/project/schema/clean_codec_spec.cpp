#include "controls_test_plugins.h"
#include "project/clean_codec.h"
#include "project/document.h"
#include "project/schema/song_schema.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/write_json.h"
#include "support/printers/preset_document_printer.h"
#include "support/printers/schema_report_printer.h"
#include "support/printers/song_document_printer.h"
#include "support/schema/generator.h"
#include "support/schema/round_trip.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <string>
#include <vector>

using namespace deluge::project;
using deluge::serialization::parseJson;
using deluge::serialization::Value;
using deluge::serialization::write_json;
namespace sch = deluge::serialization::schema;

namespace {
const CodecPlugins& plugins() {
	static const CodecPlugins held = test::test_plugins();
	return held;
}
constexpr std::string_view kFirmware = "c2.0.0-dev";

SongDocument legacy_song() {
	return *decode_song(*deluge::serialization::parseXml(R"(<song inputTickMagnitude="2" swingAmount="10">
	    <reverb roomSize="1073741824"/>
	    <sections><section id="0" numRepeats="2" midiCommandChannel="1" midiCommandNote="60"/></sections>
	    <instruments>
	        <sound id="1" presetName="LEAD"><osc1 type="square"/></sound>
	        <kit id="2" presetName="KIT"><soundSources><sound id="5" name="KICK"><osc1 type="sample"/></sound></soundSources></kit>
	    </instruments>
	    <sessionClips>
	        <instrumentClip id="10" name="A"><noteRows><noteRow y="60"/></noteRows></instrumentClip>
	    </sessionClips>
	</song>)"),
	                    plugins());
}

Value through_text(const Value& document) {
	return *parseJson(write_json(document));
}
} // namespace

// clang-format off
describe clean_codec("the clean format's envelope", ${
	it("round-trips a song read from a legacy file", _ {
		const SongDocument song = legacy_song();
		const Value written = encode_clean(song, kFirmware, plugins());
		const auto back = decode_clean(through_text(written), plugins());
		expect(back).to_have_value();
		expect(back->value).to_equal(song);
		expect(back->report.notes.size()).to_equal(std::size_t{0});
		expect(write_json(encode_clean(back->value, kFirmware, plugins()))).to_equal(write_json(written));
	});

	it("heads a document with its kind, schema revision and firmware", _ {
		const Value written = encode_clean(legacy_song(), kFirmware, plugins());
		const Value& header = *written.find("deluge");
		expect(header.find("kind")->str()).to_equal(std::string{"song"});
		expect(header.find("schema")->str()).to_equal(std::to_string(kSchemaRevision));
		expect(header.find("firmware")->str()).to_equal(std::string{kFirmware});
		expect(is_clean_document(written)).to_equal(true);
	});

	it("tells a legacy document from a clean one, and refuses it", _ {
		const Value old = *deluge::serialization::parseXml(R"(<song/>)");
		expect(is_clean_document(old)).to_equal(false);
		expect(decode_clean(old, plugins()).error().kind).to_equal(sch::LoadError::Kind::NOT_A_DOCUMENT);
	});

	it("refuses a preset as a song", _ {
		const Value preset = encode_preset_clean(extract_preset(legacy_song(), 1), 2, kFirmware, plugins());
		expect(decode_clean(preset, plugins()).error()).to_equal(sch::LoadError{sch::LoadError::Kind::WRONG_KIND, "deluge/kind"});
	});

	it("reads a document of another revision, noting it", _ {
		Value written = through_text(encode_clean(legacy_song(), kFirmware, plugins()));
		written.find_mut("deluge")->replace_or_append("schema", Value::makeNumber(kSchemaRevision + 1));
		const auto back = decode_clean(written, plugins());
		expect(back).to_have_value();
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{{sch::LoadNote::Kind::SCHEMA_REVISION_DIFFERS, "deluge/schema"}});
	});

	it("takes a kit out of its song as a preset, its drums and its own mappings with it", _ {
		SongDocument song = legacy_song();
		song.mappings.restore({.source = MidiCommand{.channel_or_zone = 3, .note_or_cc = 36}, .target = DrumMuteTarget{5}});
		song.mappings.restore({.source = MidiCommand{.channel_or_zone = 3, .note_or_cc = 37}, .target = ClipMuteTarget{10}});
		const PresetDocument preset = extract_preset(song, 2);
		expect(preset.output_order.size()).to_equal(std::size_t{1});
		expect(preset.drums.size()).to_equal(std::size_t{1});
		expect(preset.mappings.entries().size()).to_equal(std::size_t{1});
		const auto back = decode_preset_clean(through_text(encode_preset_clean(preset, 2, kFirmware, plugins())), plugins());
		expect(back).to_have_value();
		expect(back->value).to_equal(preset);
	});

	it("reads a preset's one output and drops a second", _ {
		const Value one = through_text(encode_preset_clean(extract_preset(legacy_song(), 1), 2, kFirmware, plugins()));
		Value two = one;
		Value outputs = *one.find("preset")->find("outputs");
		Value second = outputs.items()[0];
		second.replace_or_append("id", Value::makeNumber(9));
		outputs.append(second);
		two.find_mut("preset")->replace_or_append("outputs", outputs);
		const auto back = decode_preset_clean(through_text(two), plugins());
		expect(back).to_have_value();
		expect(back->value.output_order.size()).to_equal(std::size_t{1});
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{
		    {sch::LoadNote::Kind::TOO_MANY_ITEMS, "preset/outputs/1"}});
	});

	it("round-trips generated songs", _ {
		const schema::CleanContext clean{&plugins(), 1};
		for (uint32_t seed = 1; seed <= 100; ++seed) {
			const auto checked = sch::test::round_trip(schema::kSongSchema,
			    sch::test::Generator{seed}.record(schema::kSongDesc), &clean);
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int clean_codec_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(clean_codec).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
