#include "cppspec.hpp"
#include "gui/l10n/strings.h" // deluge::l10n::String enumerators -- util/notify.h only forward-declares the type
#include "support/spec_main.h"
#include "util/notify.h"

using namespace deluge::base::notify;

// clang-format off
describe typed_notify("notify typed raisers", $ {
	it("round-trips Error and l10n String through the plain fields", _ {
		Notice out{};
		while (notices().pop(out)) {}                       // drain anything earlier specs left
		notice(Error::INSUFFICIENT_RAM);
		notice(deluge::l10n::String::STRING_FOR_ERROR_SD_CARD, Notice::Kind::Console);
		expect(notices().pop(out)).to_be_true();
		expect(error_of(out)).to_equal(Error::INSUFFICIENT_RAM);
		expect(notices().pop(out)).to_be_true();
		expect(string_of(out)).to_equal(deluge::l10n::String::STRING_FOR_ERROR_SD_CARD);
		expect(out.kind).to_equal(Notice::Kind::Console);
	});
});

DELUGE_SPEC(typed_notify_spec, typed_notify)
