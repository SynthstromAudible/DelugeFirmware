#include "scenes/l10n_bridge.h"

#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
// The legacy switch this file transcribes (src/deluge/hid/display/display.cpp:7). The scenes
// LIBRARY may not include hid/ -- this SPEC may, to compare the port against the original.
#include "hid/display/display.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#include "spec/support/spec_main.h"

// getErrorMessage has external linkage (display.cpp:7) but no header declares it -- oled.cpp
// (hid/display/oled.cpp:1171) forward-declares it the same way, in the same namespace, to call it.
namespace deluge::hid::display {
extern std::string_view getErrorMessage(Error error);
}

using namespace deluge;

// clang-format off
describe scenes_l10n_bridge("scenes::l10n_bridge", $ {
	// The real app selects a language during boot (OLED()'s constructor, hid/display/oled.h:39,
	// runs once the app constructs its Display) -- this spec calls l10n::getView directly without
	// booting the app, so `l10n::chosenLanguage` is otherwise left null (l10n.cpp:6) and dereferencing
	// it segfaults. Pick the built-in English table the same way OLED() does.
	before_all([] { l10n::chosenLanguage = &l10n::built_in::english; });

	it("text_lookup matches l10n::getView for the same string id", _ {
		expect(std::string(scenes::text_lookup(static_cast<uint16_t>(l10n::String::STRING_FOR_LPF))))
		    .to_equal(std::string(l10n::getView(l10n::String::STRING_FOR_LPF)));
	});

	it("error_lookup matches the legacy getErrorMessage for a representative code", _ {
		expect(std::string(scenes::error_lookup(static_cast<int16_t>(Error::SD_CARD_FULL))))
		    .to_equal(std::string(hid::display::getErrorMessage(Error::SD_CARD_FULL)));
	});

	it("every Error value 0..32 yields a non-empty string, matching the legacy switch exactly", _ {
		for (int16_t code = 0; code <= 32; code++) {
			const std::string ported(scenes::error_lookup(code));
			const std::string legacy(hid::display::getErrorMessage(static_cast<Error>(code)));
			expect(ported.empty()).to_be_false();
			expect(ported).to_equal(legacy);
		}
	});
});
// clang-format on

DELUGE_SPEC(l10n_bridge_spec, scenes_l10n_bridge)
