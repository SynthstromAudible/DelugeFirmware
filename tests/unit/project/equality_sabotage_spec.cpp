#include "controls_test_plugins.h"
#include "effects/plugin.h"
#include "equality/equality.h"
#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/params.h"
#include "instruments/classic/words.h"
#include "serialization/parse_xml.h"
#include "serialization/write_json.h"
#include "support/record_state.h"

#include <cppspec.hpp>
#include <fstream>
#include <sstream>
#include <string>

using namespace deluge::project;

namespace {
std::string fixture() {
	std::ifstream in(DELUGE_MODEL_RECORDS);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}
} // namespace

namespace {
/// @brief @p node with the first entry named @p key below it left out.
/// @param node    The tree to copy.
/// @param key     The entry name to drop.
/// @param dropped In/out: set once an entry has been dropped; later matches are kept.
/// @return The copy of @p node without that entry.
deluge::serialization::Value without_first(const deluge::serialization::Value& node, std::string_view key,
                                           bool& dropped) {
	if (node.isMap()) {
		auto map = deluge::serialization::Value::makeMap();
		for (const auto& [name, value] : node.entries()) {
			if (!dropped && name == key) {
				dropped = true;
				continue;
			}
			map.set(name, without_first(value, key, dropped));
		}
		return map;
	}
	if (node.isArray()) {
		auto array = deluge::serialization::Value::makeArray();
		for (const deluge::serialization::Value& item : node.items()) {
			array.append(without_first(item, key, dropped));
		}
		return array;
	}
	return node;
}
} // namespace

// clang-format off
describe equality_sabotage("the model-equality gate", ${
	it("passes the song holding every record", _ {
		auto result = check_equality(fixture(), false);
		expect(result.error_or("")).to_equal(std::string{});
	});

	it("fails, naming the record, when encode drops a field", _ {
		EqualityFaults faults;
		faults.on_encoded = [](deluge::serialization::Value& document) {
			auto song = deluge::serialization::Value::makeMap();
			for (const auto& [key, value] : document.find("song")->entries()) {
				if (key != "swingAmount") {
					song.set(key, value);
				}
			}
			document = deluge::serialization::Value::makeMap();
			document.set("song", std::move(song));
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("groove");
	});

	it("fails, naming the record, when decode misreads a field", _ {
		EqualityFaults faults;
		faults.on_decoded = [](SongDocument& document) { document.groove.swing_amount += 1; };
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("groove");
	});

	it("finds an XML song and its JSON save the same song", _ {
		const std::string json = deluge::serialization::write_json(
		    encode_song(*decode_song(*deluge::serialization::parseXml(fixture()), deluge::project::test::test_plugins()), "c1.0.0", deluge::project::test::test_plugins()));
		auto result = compare_songs(fixture(), false, json, true);
		expect(result.error_or("")).to_equal(std::string{});
	});

	it("names the record when two saves are different songs", _ {
		auto changed = decode_song(*deluge::serialization::parseXml(fixture()), deluge::project::test::test_plugins());
		changed->groove.swing_amount += 1;
		const std::string json = deluge::serialization::write_json(encode_song(*changed, "c1.0.0", deluge::project::test::test_plugins()));
		auto result = compare_songs(fixture(), false, json, true);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("groove");
	});

	it("finds two documents with the same tree the same, whatever their whitespace", _ {
		auto result = compare_trees(R"(<sound a="1"><osc1 type="sine"/></sound>)", false,
		                            "<sound\n  a=\"1\">\n<osc1\n type=\"sine\"></osc1></sound>", false);
		expect(result.error_or("")).to_equal(std::string{});
	});

	it("names the first path where two trees differ", _ {
		auto result = compare_trees(R"(<sound a="1"><osc1 type="sine"/></sound>)", false,
		                            R"(<sound a="1"><osc1 type="saw"/></sound>)", false);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("sound/osc1/type");
	});

	it("names a kit's effects when a slot is dropped", _ {
		EqualityFaults faults;
		faults.on_decoded = [](SongDocument& document) {
			for (auto& [id, output] : document.outputs) {
				if (output.effects && !output.effects->slots.empty()) {
					output.effects->slots.pop_back();
					return;
				}
			}
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("effects");
	});

	it("names the master's effects when an effect setting changes", _ {
		EqualityFaults faults;
		faults.on_decoded = [](SongDocument& document) {
			document.master_effects.effects.as<deluge::effects::DelugeEffects>()->mod_fx.type =
			    deluge::effects::ModFXType::CHORUS;
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("master effects");
	});

	it("fails, naming the output, when a synth track's sound changes", _ {
		EqualityFaults faults;
		faults.on_decoded = [](SongDocument& document) {
			for (auto& [id, output] : document.outputs) {
				if (output.instrument) {
					deluge::test::set(*output.instrument->state.as<deluge::instruments::classic::DelugeSynth>(), "mode",
					                  deluge::instruments::classic::to_string(deluge::instruments::classic::SynthMode::FM));
					return;
				}
			}
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("output");
	});

	it("fails, naming the output, when a mod knob is pointed elsewhere", _ {
		EqualityFaults faults;
		faults.on_decoded = [](SongDocument& document) {
			for (auto& [id, output] : document.outputs) {
				for (ControllerMapping& mapping : output.controls) {
					mapping.target = deluge::project::engine_target(
					    (mapping.target.id == deluge::instruments::classic::param::kPan
					         ? deluge::instruments::classic::param::kVolumePostFX
					         : deluge::instruments::classic::param::kPan)
					        .value);
					return;
				}
			}
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("output");
	});

	it("fails, naming the mappings, when a learned MIDI knob is lost", _ {
		EqualityFaults faults;
		faults.on_encoded = [](deluge::serialization::Value& document) {
			bool dropped = false;
			document = without_first(document, "midiKnobs", dropped);
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("mappings");
	});

	it("fails, naming the drum, when a sound kit row's sound changes", _ {
		EqualityFaults faults;
		faults.on_decoded = [](SongDocument& document) {
			for (auto& [id, drum] : document.drums) {
				if (drum.instrument) {
					deluge::test::set(*drum.instrument->state.as<deluge::instruments::classic::DelugeSynth>(), "mode",
					                  deluge::instruments::classic::to_string(deluge::instruments::classic::SynthMode::FM));
					return;
				}
			}
		};
		auto result = check_equality(fixture(), false, faults);
		expect(result.has_value()).to_be_false();
		expect(result.error_or("")).to_contain("drum");
	});
});

CPPSPEC_SPEC(equality_sabotage)
