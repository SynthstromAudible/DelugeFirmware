#include "../support/spec_main.h"
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
		// The clipNeedsReRendering() overrides ClipContent replaced re-rendered the sidebar too.
		expect(r.sidebar_rows).to_be_true();
	});
	it("is empty for an empty set", _ {
		DirtySet s;
		LegacyRedraw r = legacy_redraw_for(s.drain());
		expect(r.main_rows || r.sidebar_rows || r.oled).to_be_false();
	});
});
DELUGE_SPEC(notify_drain_mapping_spec, drain_mapping)
