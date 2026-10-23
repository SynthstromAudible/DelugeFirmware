#include "gui/ui/menu_view_of.h"

#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"

namespace deluge::gui {

scenes::services::MenuView menuViewOf(::Song& song, ::Clip* clip) {
	(void)song;
	scenes::services::MenuView view;
	if (clip == nullptr || clip->output == nullptr || !clip->output->id()) {
		return view;
	}
	using scenes::bind::TrackKind;
	view.clip = project::ClipAddress{clip->id()};
	view.track = project::TrackAddress{*clip->output->id()};
	view.audio_clip = clip->type == ClipType::AUDIO;
	switch (clip->output->type) {
	case OutputType::KIT:
		view.kind = TrackKind::KIT;
		break;
	case OutputType::MIDI_OUT:
		view.kind = TrackKind::MIDI_OUT;
		break;
	case OutputType::CV:
		view.kind = TrackKind::CV;
		break;
	case OutputType::AUDIO:
		view.kind = TrackKind::AUDIO;
		break;
	default:
		view.kind = TrackKind::SYNTH;
		break;
	}
	if (clip->type == ClipType::INSTRUMENT) {
		auto* instrumentClip = static_cast<InstrumentClip*>(clip);
		view.affect_entire = instrumentClip->affectEntire;
		if (clip->output->type == OutputType::KIT) {
			const Drum* selected = static_cast<Kit*>(clip->output)->selectedDrum;
			if (selected != nullptr && selected->id()) {
				view.selected_row = *selected->id();
				view.selected_row_type = selected->type;
				view.row_in_clip = instrumentClip->getNoteRowForDrum(const_cast<Drum*>(selected)) != nullptr;
			}
		}
	}
	return view;
}

} // namespace deluge::gui
