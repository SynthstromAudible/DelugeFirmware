#include "gui/ui/notify_drain.h"
#include <initializer_list>

LegacyRedraw legacy_redraw_for(const deluge::notify::DirtySet& d) {
	using deluge::notify::Dirty;
	LegacyRedraw r;
	auto any = [&](std::initializer_list<Dirty> ds) {
		for (auto x : ds)
			if (d.has(x))
				return true;
		return false;
	};
	r.main_rows = any({Dirty::GridMain, Dirty::ClipContent, Dirty::NoteRows, Dirty::SampleReady, Dirty::PlaybackState,
	                   Dirty::SongSwapped, Dirty::OutputChanged, Dirty::SelectedDrum});
	// ClipContent/NoteRows also mark the sidebar dirty: InstrumentClipView::clipNeedsReRendering()
	// re-renders its sidebar too, and SessionView's grid layout re-renders main + sidebar together.
	// Over-invalidation for the layouts that only wanted main rows, never loss.
	r.sidebar_rows = any({Dirty::GridSidebar, Dirty::OutputChanged, Dirty::SelectedDrum, Dirty::PlaybackState,
	                      Dirty::MidiLearn, Dirty::ClipContent, Dirty::NoteRows});
	r.oled = any({Dirty::Oled, Dirty::OutputChanged, Dirty::ParamAutomation, Dirty::ModTarget, Dirty::SongSwapped,
	              Dirty::PlaybackState, Dirty::SampleReady});
	return r;
}
