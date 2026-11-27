#include "controls_test_plugins.h"
#include "project/document.h"
#include "project/schema/output_schema.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
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
inline constexpr std::array kOutputsOnlyFields{kOutputsField};
inline constexpr sch::RecordDesc kOutputsOnlyDesc = sch::closed_record("outputsOnly", kOutputsOnlyFields);
inline constexpr sch::Schema<SongDocument> kOutputsOnly{&kOutputsOnlyDesc};

const CodecPlugins& plugins() {
	static const CodecPlugins held = test::test_plugins();
	return held;
}

/// A song read from @p xml by the legacy decoder: outputs whose instrument and chain states are real.
SongDocument legacy(std::string_view xml) {
	return *decode_song(*deluge::serialization::parseXml(xml), plugins());
}

SongDocument through_clean(const SongDocument& song) {
	const CleanContext clean{&plugins(), song.transport.input_tick_magnitude()};
	const Value written = sch::encode(kOutputsOnly, song, &clean);
	return sch::decode(kOutputsOnly, *parseJson(write_json(written)), &clean)->value;
}
} // namespace

// clang-format off
describe output_schema("the outputs' schema", ${
	it("round-trips one output of every kind, a kit's drums of every kind among them", _ {
		const SongDocument song = legacy(R"(<song inputTickMagnitude="1"><instruments>
		    <sound id="1" presetName="LEAD" colour="3"><osc1 type="square"/><arpeggiator arpMode="arp" numOctaves="3"/></sound>
		    <kit id="2" presetName="KIT"><soundSources>
		        <sound id="5" name="KICK"><osc1 type="sample"/></sound>
		        <midiOutput id="6" name="M" channel="2" note="40"/>
		        <gateOutput id="7" name="G" channel="1"/>
		    </soundSources></kit>
		    <audioTrack id="3" name="VOX" mode="Looper/FX" inputChannel="left"/>
		    <midiChannel id="4" channel="9" suffix="2"/>
		    <cvChannel id="8" channel="1" cv2Source="2"/>
		</instruments></song>)");
		expect(song.output_order.size()).to_equal(std::size_t{5});
		const SongDocument back = through_clean(song);
		expect(back.output_order).to_equal(song.output_order);
		expect(back.outputs).to_equal(song.outputs);
		expect(back.drums).to_equal(song.drums);
	});

	it("writes each output as a record of its kind, an instrument as its plugin node", _ {
		const SongDocument song = legacy(R"(<song><instruments><sound id="1" presetName="LEAD"><osc1 type="square"/></sound></instruments></song>)");
		const CleanContext clean{&plugins(), song.transport.input_tick_magnitude()};
		const Value written = sch::encode(kOutputsOnly, song, &clean);
		const Value& sound = written.find("outputs")->items()[0];
		expect(sound.find("kind")->str()).to_equal(std::string{"sound"});
		expect(sound.find("instrument")->find("plugin")->str()).to_equal(std::string{"deluge.instrument"});
		const Value& state = *sound.find("instrument")->find("state");
		expect(state.find("version")->str()).to_equal(std::to_string(kInstrumentStateVersion));
		expect(state.find("entries")->isMap()).to_equal(true);
	});

	it("decodes an instrument whose state comes before its plugin id", _ {
		const SongDocument song = legacy(R"(<song><instruments><sound id="1" presetName="LEAD"><osc1 type="square"/></sound></instruments></song>)");
		const CleanContext clean{&plugins(), song.transport.input_tick_magnitude()};
		const Value written = sch::encode(kOutputsOnly, song, &clean);
		const Value& slot = *written.find("outputs")->items()[0].find("instrument");
		Value reordered = Value::makeMap();
		for (const auto& [key, value] : slot.entries()) {
			if (key != "plugin") {
				reordered.set(key, value);
			}
		}
		reordered.set("plugin", *slot.find("plugin"));
		Value sound = written.find("outputs")->items()[0];
		sound.replace_or_append("instrument", reordered);
		Value outputs = Value::makeArray();
		outputs.append(sound);
		Value root = Value::makeMap();
		root.set("outputs", outputs);
		const auto back = sch::decode(kOutputsOnly, *parseJson(write_json(root)), &clean);
		expect(back->report.notes.size()).to_equal(std::size_t{0});
		expect(back->value.outputs.at(1).instrument->state.has_value()).to_be_true();
		expect(back->value.outputs.at(1).instrument).to_equal(song.outputs.at(1).instrument);
	});

	it("keeps an instrument no plugin reads, and writes it back", _ {
		const CleanContext clean{&plugins(), 1};
		const auto back = sch::decode(kOutputsOnly, *parseJson(R"({"outputs": [{"kind": "sound", "id": 1,
		    "instrument": {"plugin": "acme.synth", "routing": {"sideChainSend": 0, "midiChannel": null, "midiNote": null},
		                   "state": {"version": 3, "tickMagnitude": 1, "entries": {"wobble": 9}}}}]})"), &clean);
		expect(back).to_have_value();
		expect(back->report.notes).to_equal(std::vector<sch::LoadNote>{
		    {sch::LoadNote::Kind::PLUGIN_STATE_KEPT, "outputs/0/instrument/state"}});
		const Value again = sch::encode(kOutputsOnly, back->value, &clean);
		const Value& state = *again.find("outputs")->items()[0].find("instrument")->find("state");
		expect(state.find("entries")->find("wobble")->str()).to_equal(std::string{"9"});
	});

	it("refuses two drums with one id across kits", _ {
		const CleanContext clean{&plugins(), 1};
		const auto back = sch::decode(kOutputsOnly, *parseJson(R"({"outputs": [
		    {"kind": "kit", "id": 1, "drums": [{"kind": "gateOutput", "id": 5}]},
		    {"kind": "kit", "id": 2, "drums": [{"kind": "gateOutput", "id": 5}]}]})"), &clean);
		expect(back.error()).to_equal(sch::LoadError{sch::LoadError::Kind::DUPLICATE_ID, "outputs/1/drums/0"});
	});

	it("round-trips generated outputs", _ {
		const CleanContext clean{&plugins(), 1};
		for (uint32_t seed = 1; seed <= 200; ++seed) {
			const auto checked = sch::test::round_trip(kOutputsOnly, sch::test::Generator{seed}.record(kOutputsOnlyDesc), &clean);
			expect(checked.error_or(std::string{})).to_equal(std::string{});
		}
	});
});
// clang-format on

int output_schema_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(output_schema).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
