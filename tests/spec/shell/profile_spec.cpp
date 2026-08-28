#include "shell/fixed_vector.h"
#include "shell/profile.h"

#include "cppspec.hpp"

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
});
// clang-format on

DELUGE_SPEC(shell_profile_spec, shell_profile)
