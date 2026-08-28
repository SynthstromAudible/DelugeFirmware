#include "../support/spec_main.h"
#include "cppspec.hpp"
#include "gui/l10n/strings.h" // deluge::l10n::String enumerators -- util/notify.h only forward-declares the type
#include "util/notify.h"

using namespace deluge::notify;

// clang-format off
describe typed_notify("notify typed raisers", $ {
	it("round-trips Error and l10n String through the plain fields", _ {
		Notice out{};
		while (notices().pop(out)) {}                       // drain anything earlier specs left
		notice(Error::INSUFFICIENT_RAM);
		notice(deluge::l10n::String::STRING_FOR_ERROR_SD_CARD, Notice::Kind::Console);
		expect(notices().pop(out)).to_be_true();
		expect(error_of(out) == Error::INSUFFICIENT_RAM).to_be_true();
		expect(notices().pop(out)).to_be_true();
		expect(string_of(out) == deluge::l10n::String::STRING_FOR_ERROR_SD_CARD).to_be_true();
		expect(out.kind == Notice::Kind::Console).to_be_true();
	});
});

DELUGE_SPEC(notify_typed_notify_spec, typed_notify)
