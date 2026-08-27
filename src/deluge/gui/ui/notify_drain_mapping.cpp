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
	r.sidebar_rows =
	    any({Dirty::GridSidebar, Dirty::OutputChanged, Dirty::SelectedDrum, Dirty::PlaybackState, Dirty::MidiLearn});
	r.oled = any({Dirty::Oled, Dirty::OutputChanged, Dirty::ParamAutomation, Dirty::ModTarget, Dirty::SongSwapped,
	              Dirty::PlaybackState, Dirty::SampleReady});
	return r;
}
