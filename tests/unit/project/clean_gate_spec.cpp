#include "clean/clean_check.h"

#include <cppspec.hpp>

#include <cstdlib>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace deluge::project;

namespace {
std::string read(const char* path) {
	std::ifstream in(path, std::ios::binary);
	std::stringstream text;
	text << in.rdbuf();
	return text.str();
}
/// A song whose sound's arp holds a value outside its row's range: the legacy decode stores it, the clean load clamps
/// it.
constexpr std::string_view kClampedArp = R"(<song><instruments><sound id="1" presetName="A"><osc1 type="square"/>
    <arpeggiator arpMode="arp" numOctaves="0"/></sound></instruments></song>)";
} // namespace

// clang-format off
describe clean_gate("the clean-roundtrip gate", ${
	it("passes the document holding every record the model decodes", _ {
		const auto checked = check_clean(read(DELUGE_MODEL_RECORDS), false);
		expect(checked.has_value() ? std::string{} : checked.error()).to_equal(std::string{});
		expect(checked->presets).to_be_greater_than(std::size_t{0});
	});

	it("passes a value the clean load clamped, and names it", _ {
		const auto checked = check_clean(kClampedArp, false);
		expect(checked).to_have_value();
		expect(checked->clamped).to_contain(std::string{"song/outputs/0/arp/numOctaves"});
	});

	it("catches a field the clean save loses", _ {
		const auto checked = check_clean(read(DELUGE_MODEL_RECORDS), false, CleanFaults{.on_encoded = [](auto& document) {
			(void)document.find_mut("song")->erase("outputs");
		}});
		expect(checked).not_().to_have_value();
	});

	it("catches a record the clean load changes", _ {
		const auto checked = check_clean(read(DELUGE_MODEL_RECORDS), false,
		    CleanFaults{.on_decoded = [](SongDocument& song) { song.mod_knob_mode = 7; }});
		expect(checked).not_().to_have_value();
	});
});
// clang-format on

int clean_gate_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(clean_gate).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
