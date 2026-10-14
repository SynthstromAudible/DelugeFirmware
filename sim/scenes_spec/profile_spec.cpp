#include "scenes/profile.h"

#include "gui/l10n/l10n.h"
#include "scenes/l10n_bridge.h"
#include "shell/profiles/deluge.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>

#include "spec/support/spec_main.h"

using namespace deluge;

namespace {

/// @brief Do two spans view the same storage? Compared by `.data()` and `.size()` (ruling Q3) --
///        `std::span` has no `operator==`, and the shell's spans are supposed to be the SAME
///        static storage the shell's own profile views, not merely equal-valued copies.
template <typename T>
bool same_span(std::span<T> a, std::span<T> b) {
	return a.data() == b.data() && a.size() == b.size();
}

} // namespace

// clang-format off
describe scenes_profile("scenes::deluge_profile", $ {
	// See l10n_bridge_spec.cpp's before_all: text_or_digits below resolves through scenes::text_lookup,
	// which needs `l10n::chosenLanguage` set (normally done by the app's Display construction at boot).
	before_all([] { l10n::chosenLanguage = &l10n::built_in::english; });

	it("wires the l10n bridge the shell's own profile deliberately leaves null", _ {
		const ui::shell::BoardProfile& p = scenes::deluge_profile();
		expect(p.text == &scenes::text_lookup).to_be_true();
		expect(p.error_text == &scenes::error_lookup).to_be_true();
	});

	it("copies every other field from ui::shell::deluge_profile(), unchanged", _ {
		const ui::shell::BoardProfile& p = scenes::deluge_profile();
		const ui::shell::BoardProfile& shell_p = ui::shell::deluge_profile();

		expect(same_span(p.button_by_matrix, shell_p.button_by_matrix)).to_be_true();
		expect(same_span(p.led_by_button, shell_p.led_by_button)).to_be_true();
		expect(same_span(p.encoder_by_index, shell_p.encoder_by_index)).to_be_true();
		expect(same_span(p.encoder_kind, shell_p.encoder_kind)).to_be_true();
		expect(same_span(p.oled_role_by_index, shell_p.oled_role_by_index)).to_be_true();
		expect(same_span(p.style_by_index, shell_p.style_by_index)).to_be_true();
		expect(p.button_cols).to_equal(shell_p.button_cols);
		expect(p.button_rows).to_equal(shell_p.button_rows);
		expect(p.sidebar_cols).to_equal(shell_p.sidebar_cols);
	});

	it("text_or_digits resolves through the wired hook, not the digit fallback", _ {
		const ui::shell::BoardProfile& p = scenes::deluge_profile();
		ui::shell::TextScratch s{};
		const auto string_id = static_cast<uint16_t>(l10n::String::STRING_FOR_LPF);
		expect(std::string(p.text_or_digits(string_id, s))).to_equal(std::string(scenes::text_lookup(string_id)));
	});
});
// clang-format on

DELUGE_SPEC(profile_spec, scenes_profile)
