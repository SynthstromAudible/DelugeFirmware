#include "instruments/classic/codec.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/tree.h"
#include "support/printers/plugin_result_printer.h"

#include <cppspec.hpp>
#include <expected>
#include <optional>
#include <string>

using namespace deluge::instruments::classic;

namespace {

/// A new song's codec context, whose input tick magnitude is 0.
constexpr deluge::plugin::CodecContext kNewSong{.input_tick_magnitude = 0};

/// What decoding the sound an XML document holds into a new synth's state gives.
std::expected<void, deluge::plugin::StateError> decodeXml(const std::string& xml) {
	auto doc = deluge::serialization::parseXml(xml);
	SoundState state;
	if (!doc) {
		return std::unexpected(deluge::plugin::StateError::NOT_A_MAP);
	}
	return decode(*doc->find("sound"), state, kNewSong);
}

/// A sound in @p mode holding a DX7 on @p slot, as released firmware wrote one, the other slot a square.
std::string dx7Sound(const std::string& mode, const std::string& slot) {
	const std::string dx = R"(type="dx7" transpose="3" dx7patch="00" dx7enginemode="2")";
	const std::string other = R"(type="square")";
	return "<sound mode=\"" + mode + "\"><osc1 " + (slot == "osc1" ? dx : other) + "/><osc2 "
	       + (slot == "osc2" ? dx : other) + "/></sound>";
}

const std::expected<void, deluge::plugin::StateError> kRefused =
    std::unexpected(deluge::plugin::StateError::LEGACY_FORM);

} // namespace

// clang-format off
describe dx7_forms("a DX7 on an oscillator, as released firmware wrote one", ${
	it("is refused on either slot of a subtractive sound: the upgrader converts it to the DX7 engine", _ {
		expect(decodeXml(dx7Sound("subtractive", "osc1"))).to_equal(kRefused);
		expect(decodeXml(dx7Sound("subtractive", "osc2"))).to_equal(kRefused);
		expect(decodeXml(R"(<sound><osc1 type="dx7"/></sound>)")).to_equal(kRefused);
	});

	it("is refused in ring modulation and FM, which never played it", _ {
		expect(decodeXml(dx7Sound("ringmod", "osc1"))).to_equal(kRefused);
		expect(decodeXml(dx7Sound("fm", "osc2"))).to_equal(kRefused);
	});

	it("is refused in the JSON form as in the XML form", _ {
		auto doc = deluge::serialization::parseJson(
		    R"({"sound": {"mode": "subtractive", "osc1": {"type": "dx7", "dx7patch": "00"}, "osc2": {"type": "square"}}})");
		expect(doc.has_value()).to_equal(true);
		SoundState state;
		expect(decode(*doc->find("sound"), state, kNewSong)).to_equal(kRefused);
	});

	it("leaves every other oscillator type read", _ {
		expect(decodeXml(R"(<sound mode="subtractive"><osc1 type="saw"/><osc2 type="sine"/></sound>)")).to_have_value();
		expect(decodeXml(R"(<sound mode="dx7"><dx7 patch="00"/></sound>)")).to_have_value();
	});
});
// clang-format on

CPPSPEC_SPEC(dx7_forms)
