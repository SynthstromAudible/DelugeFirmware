#include "cppspec.hpp"
#include "gui/ui/notify_drain.h"

using namespace deluge::notify;

// clang-format off
describe drain_mapping("legacy_redraw_for", $ {
	it("maps grid facts to rows and oled facts to the display", _ {
		DirtySet s; s.raise(Dirty::GridSidebar);
		LegacyRedraw r = legacy_redraw_for(s.drain());
		expect(r.sidebar_rows).to_be_true(); expect(r.main_rows).to_be_false(); expect(r.oled).to_be_false();
		s.raise(Dirty::Oled);
		r = legacy_redraw_for(s.drain());
		expect(r.oled).to_be_true(); expect(r.main_rows).to_be_false();
		s.raise_clip(Dirty::ClipContent, 2);
		r = legacy_redraw_for(s.drain());
		expect(r.main_rows).to_be_true();
	});
	it("is empty for an empty set", _ {
		DirtySet s;
		LegacyRedraw r = legacy_redraw_for(s.drain());
		expect(r.main_rows || r.sidebar_rows || r.oled).to_be_false();
	});
});
// clang-format on

// Not CPPSPEC_SPEC(drain_mapping): create_test_sourcelist derives the extern function name from
// this file's path relative to tests/spec (slashes become underscores), i.e. `notify_drain_mapping_spec`
// for notify/drain_mapping_spec.cpp, which does not match the `drain_mapping` describe-block name the
// macro would otherwise assume. Spelled out by hand so the exported symbol matches what the generated
// driver (all_specs.cpp) declares `extern` (see tests/spec/notify/dirty_set_spec.cpp for the same pattern).
int notify_drain_mapping_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(drain_mapping).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
