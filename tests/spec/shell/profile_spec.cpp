#include "shell/fixed_vector.h"
#include "shell/profile.h"

#include "cppspec.hpp"

#include <string>
#include <string_view>

#include "../support/spec_main.h"
using namespace deluge::ui::shell;

// clang-format off
describe shell_profile("shell::profile", $ {
	it("ButtonSet holds and clears ids", _ {
		ButtonSet s;
		s.set(ButtonId::Shift, true);
		expect(s.has(ButtonId::Shift)).to_be_true();
		expect(s.has(ButtonId::Play)).to_be_false();
		s.set(ButtonId::Shift, false);
		expect(s.any()).to_be_false();
	});
	it("FixedVector refuses past capacity", _ {
		FixedVector<int, 2> v;
		expect(v.push_back(1)).to_be_true(); expect(v.push_back(2)).to_be_true(); expect(v.push_back(3)).to_be_false();
		expect(v.size()).to_equal(size_t(2));
	});
	it("names every button", _ {
		for (int i = 0; i < int(ButtonId::Count); i++) { expect(std::string_view(button_name(ButtonId(i))).empty()).to_be_false(); }
	});
	it("resolves text through the profile's hooks, and as digits when they are null", _ {
		TextScratch scratch{};
		BoardProfile wired{};
		wired.text = +[](uint16_t) -> std::string_view { return "STRING"; };
		wired.error_text = +[](int16_t) -> std::string_view { return "ERROR TEXT"; };
		expect(std::string(wired.text_or_digits(42, scratch))).to_equal(std::string("STRING"));
		expect(std::string(wired.error_text_or_digits(7, scratch))).to_equal(std::string("ERROR TEXT"));

		// The documented fallback: the id itself, in decimal -- never an empty string, which would
		// draw a correctly framed but completely blank popup box.
		BoardProfile bare{};
		expect(std::string(bare.text_or_digits(42, scratch))).to_equal(std::string("42"));
		expect(std::string(bare.text_or_digits(0, scratch))).to_equal(std::string("0"));
		// Both extremes of each id type, since the scratch buffer is sized for exactly these.
		expect(std::string(bare.text_or_digits(65535, scratch))).to_equal(std::string("65535"));
		expect(std::string(bare.error_text_or_digits(-1, scratch))).to_equal(std::string("-1"));
		expect(std::string(bare.error_text_or_digits(-32768, scratch))).to_equal(std::string("-32768"));
		expect(std::string(bare.error_text_or_digits(32767, scratch))).to_equal(std::string("32767"));
	});
});
// clang-format on

DELUGE_SPEC(shell_profile_spec, shell_profile)
