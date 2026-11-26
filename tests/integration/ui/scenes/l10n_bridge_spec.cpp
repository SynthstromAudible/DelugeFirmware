#include "ui/scenes/l10n_bridge.h"

#include "base/error.h"
#include "gui/l10n/error_messages.h"
#include "gui/l10n/l10n.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "support/spec_main.h"

using namespace deluge;

// clang-format off
describe scenes_l10n_bridge("scenes::l10n_bridge", $ {
	// The real app selects a language during boot (OLED()'s constructor, hid/display/oled.h,
	// runs once the app constructs its Display) -- this spec calls l10n::getView directly without
	// booting the app, so `l10n::chosenLanguage` is otherwise left null (l10n.cpp) and dereferencing
	// it segfaults. Pick the built-in English table the same way OLED() does.
	before_all([] { l10n::chosenLanguage = &l10n::built_in::english; });

	it("text_lookup matches l10n::getView for the same string id", _ {
		expect(std::string(ui::scenes::text_lookup(static_cast<uint16_t>(l10n::String::STRING_FOR_LPF))))
		    .to_equal(std::string(l10n::getView(l10n::String::STRING_FOR_LPF)));
	});

	it("error_lookup shows the chosen language's text for a code, and the generic text for one with none", _ {
		expect(std::string(ui::scenes::error_lookup(static_cast<int16_t>(Error::SD_CARD_FULL))))
		    .to_equal(std::string("SD card full"));
		expect(std::string(ui::scenes::error_lookup(static_cast<int16_t>(Error::FILE_NOT_FOUND))))
		    .to_equal(std::string("File not found"));
		expect(std::string(ui::scenes::error_lookup(static_cast<int16_t>(Error::INVALID_SYSEX_FORMAT))))
		    .to_equal(std::string("You've encountered an unspecified error. Please report this bug."));
	});

	it("every Error value yields a non-empty string, the one l10n::error_message gives", _ {
		for (int16_t code = 0; code < static_cast<int16_t>(deluge::kErrorCount); code++) {
			const std::string ported(ui::scenes::error_lookup(code));
			const std::string mapped(l10n::error_message(static_cast<Error>(code)));
			expect(ported.empty()).to_be_false();
			expect(ported).to_equal(mapped);
		}
	});
});
// clang-format on

DELUGE_SPEC(l10n_bridge_spec, scenes_l10n_bridge)
