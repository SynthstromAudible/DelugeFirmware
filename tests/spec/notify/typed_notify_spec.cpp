#include "cppspec.hpp"
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

// Not CPPSPEC_SPEC(typed_notify): create_test_sourcelist derives the extern function name from
// this file's path relative to tests/spec (slashes become underscores), i.e.
// `notify_typed_notify_spec` for notify/typed_notify_spec.cpp, which does not match the
// `typed_notify` describe-block name the macro would otherwise assume. Spelled out by hand so the
// exported symbol matches what the generated driver (all_specs.cpp) declares `extern` (see
// tests/spec/notify/dirty_set_spec.cpp for the same pattern).
int notify_typed_notify_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(typed_notify).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
