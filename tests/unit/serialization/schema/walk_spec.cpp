#include "serialization/parse_json.h"
#include "serialization/schema/codec.h"
#include "serialization/schema/lookup.h"
#include "serialization/write_json.h"
#include "support/printers/schema_report_printer.h"
#include "support/printers/value_printer.h"
#include "test_schemas.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <string>
#include <vector>

using namespace deluge::serialization;
using namespace deluge::serialization::schema;
using namespace deluge::serialization::schema::test;

namespace {
Value json(std::string_view text) {
	return *parseJson(text);
}
/// The keys of @p map, in order.
std::vector<std::string> keys(const Value& map) {
	std::vector<std::string> found;
	for (const auto& [key, value] : map.entries()) {
		found.push_back(key);
	}
	return found;
}
} // namespace

// clang-format off
describe walk("the generic walk", ${
	it("round-trips a record of every shape through JSON", _ {
		const Panel panel = full_panel();
		const auto back = decode(kPanelSchema, json(write_json(encode(kPanelSchema, panel))));
		expect(back).to_have_value();
		expect(back->value).to_equal(panel);
		expect(back->report.notes.size()).to_equal(std::size_t{0});
	});

	it("writes every field in table order, a variant's kind first", _ {
		const Value written = encode(kPanelSchema, full_panel());
		expect(keys(written)).to_equal(std::vector<std::string>{"level", "colour", "lit", "name", "gain", "length",
		    "origin", "offset", "spare", "knobs", "pair", "labels", "figure", "figures", "plugin", "sparePlugin"});
		expect(keys(*written.find("figure"))).to_equal(std::vector<std::string>{"kind", "side"});
		expect(*written.find("colour")).to_equal(Value::makeScalar("blue"));
	});

	it("writes an absent optional as null and reads null back as absent", _ {
		Panel panel = full_panel();
		panel.offset.reset();
		const Value written = encode(kPanelSchema, panel);
		expect(written.find("offset")->str()).to_equal(std::string{"null"});
		expect(decode(kPanelSchema, written)->value.offset).to_equal(std::optional<int32_t>{});
	});

	it("keeps a field's default when the document lacks it", _ {
		const auto back = decode(kPanelSchema, json(R"({"level": 3})"));
		Panel expected;
		expected.level = 3;
		expect(back->value).to_equal(expected);
	});

	it("clamps an integer outside its range and notes where", _ {
		const auto back = decode(kPanelSchema, json(R"({"level": 99, "pair": [-9, 1]})"));
		expect(back->value.level).to_equal(int8_t{10});
		expect(back->value.pair[0]).to_equal(int8_t{-5});
		expect(back->report.notes).to_equal(std::vector<LoadNote>{
		    {LoadNote::Kind::CLAMPED, "level"}, {LoadNote::Kind::CLAMPED, "pair/0"}});
	});

	it("keeps a field's value for an unknown word, and notes where", _ {
		Panel onto;
		onto.colour = Colour::GREEN;
		const auto back = decode(kPanelSchema, json(R"({"colour": "purple"})"), nullptr, onto);
		expect(back->value.colour).to_equal(Colour::GREEN);
		expect(back->report.notes).to_equal(std::vector<LoadNote>{{LoadNote::Kind::UNKNOWN_WORD, "colour"}});
	});

	it("keeps an unknown key of an open record and writes it back after the known ones", _ {
		const auto back = decode(kPanelSchema, json(R"({"future": {"a": [1]}, "level": 1})"));
		expect(back->report.notes).to_equal(std::vector<LoadNote>{{LoadNote::Kind::UNKNOWN_KEY_KEPT, "future"}});
		const Value written = encode(kPanelSchema, back->value);
		expect(keys(written).back()).to_equal(std::string{"future"});
		expect(*written.find("future")).to_equal(*json(R"({"future": {"a": [1]}})").find("future"));
	});

	it("drops an unknown key of a closed record and notes where", _ {
		const auto back = decode(kPanelSchema, json(R"({"origin": {"x": 1, "z": 2}})"));
		expect(back->value.origin).to_equal(Point{.x = 1, .y = 0});
		expect(back->report.notes).to_equal(std::vector<LoadNote>{{LoadNote::Kind::UNKNOWN_KEY_DROPPED, "origin/z"}});
	});

	it("refuses a value of the wrong type, naming its path", _ {
		const auto back = decode(kPanelSchema, json(R"({"knobs": [{"number": 1}, {"number": "two"}]})"));
		expect(back).not_().to_have_value();
		expect(back.error()).to_equal(LoadError{LoadError::Kind::WRONG_TYPE, "knobs/1/number"});
	});

	it("refuses a record of several kinds whose first key is not its kind", _ {
		const auto late = decode(kPanelSchema, json(R"({"figure": {"side": 1, "kind": "square"}})"));
		expect(late.error()).to_equal(LoadError{LoadError::Kind::NO_KIND, "figure"});
		const auto unknown = decode(kPanelSchema, json(R"({"figures": [{"kind": "hexagon"}]})"));
		expect(unknown.error()).to_equal(LoadError{LoadError::Kind::UNKNOWN_KIND, "figures/0/kind"});
	});

	it("drops the items past a fixed list's end and notes where", _ {
		const auto back = decode(kPanelSchema, json(R"({"pair": [1, 2, 3]})"));
		expect(back->value.pair).to_equal(std::array<int8_t, 2>{1, 2});
		expect(back->report.notes).to_equal(std::vector<LoadNote>{{LoadNote::Kind::TOO_MANY_ITEMS, "pair/2"}});
	});

	it("refuses two items of an id-keyed list with one id", _ {
		const auto back = decode(kPanelSchema, json(R"({"labels": [{"cc": 1, "label": "a"}, {"cc": 1, "label": "b"}]})"));
		expect(back.error()).to_equal(LoadError{LoadError::Kind::DUPLICATE_ID, "labels/1"});
	});

	it("keeps a plugin node its plugin cannot read, and writes it back once", _ {
		const auto back = decode(kPanelSchema,
		    json(R"({"plugin": {"version": 1, "tickMagnitude": 0, "entries": {"w": 5}}})"));
		expect(back->report.notes).to_equal(std::vector<LoadNote>{{LoadNote::Kind::PLUGIN_STATE_KEPT, "plugin"}});
		const Value written = encode(kPanelSchema, back->value);
		expect(std::ranges::count(keys(written), std::string{"plugin"})).to_equal(std::ptrdiff_t{1});
		expect(*written.find("plugin")->find("entries")).to_equal(json(R"({"w": 5})"));
	});

	it("writes a field's own value once it holds one, not the node kept for it", _ {
		auto back = decode(kPanelSchema, json(R"({"plugin": {"version": 1, "tickMagnitude": 0, "entries": {"w": 5}}})"));
		back->value.plugin_state = 7;
		const Value written = encode(kPanelSchema, back->value);
		expect(std::ranges::count(keys(written), std::string{"plugin"})).to_equal(std::ptrdiff_t{1});
		expect(*written.find("plugin")->find("entries")).to_equal(json(R"({"v": 7})"));
	});

	it("keeps an optional plugin node its plugin cannot read, leaving the optional empty", _ {
		const auto back = decode(kPanelSchema,
		    json(R"({"sparePlugin": {"version": 1, "tickMagnitude": 0, "entries": {"w": 1}}})"));
		expect(back->value.spare_plugin).to_equal(std::optional<int32_t>{});
		expect(back->report.notes).to_equal(std::vector<LoadNote>{{LoadNote::Kind::PLUGIN_STATE_KEPT, "sparePlugin"}});
		const Value written = encode(kPanelSchema, back->value);
		expect(std::ranges::count(keys(written), std::string{"sparePlugin"})).to_equal(std::ptrdiff_t{1});
		expect(*written.find("sparePlugin")->find("entries")).to_equal(json(R"({"w": 1})"));
	});

	it("reads the keys after a fixed list it dropped items from", _ {
		const auto back = decode(kPanelSchema, json(R"({"pair": [1, 2, 3], "level": 4})"));
		expect(back->value.level).to_equal(int8_t{4});
	});

	it("refuses a plugin node that is not one", _ {
		const auto back = decode(kPanelSchema, json(R"({"plugin": [1]})"));
		expect(back.error()).to_equal(LoadError{LoadError::Kind::BAD_PLUGIN_STATE, "plugin"});
	});

	it("finds a member by key and type, and nothing for another type", _ {
		Panel panel = full_panel();
		expect(member_at<int8_t>(kPanelSchema, panel, "level")).to_equal(&panel.level);
		expect(member_at<std::optional<int32_t>>(kPanelSchema, panel, "offset")).to_equal(&panel.offset);
		expect(member_at<int32_t>(kPanelSchema, panel, "level")).to_be_null();
		expect(member_at<int8_t>(kPanelSchema, panel, "nothing")).to_be_null();
	});
});
// clang-format on

int walk_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(walk).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
