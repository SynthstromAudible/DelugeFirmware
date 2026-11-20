#include "gui/ui/shell_host/shell_host.h"
#include "model/edits/edit.h"
#include "model/song/song.h"
#include "scenes/menu/bind_host.h"

namespace deluge::scenes::menu {

const bind::Ports& host_ports() {
	static const bind::Ports ports{
	    .audio = &gui::shell_host::audio_facts(),
	    .card = &gui::shell_host::card_facts(),
	    .ui = &gui::shell_host::ui_facts(),
	    .sound = &gui::shell_host::sound_facts(),
	    .clips = &gui::shell_host::clip_facts(),
	    .tracks = &gui::shell_host::track_facts(),
	    .controls = &gui::shell_host::control_facts(),
	    .clip_writes = &gui::shell_host::clip_writes(),
	    .sound_writes = &gui::shell_host::sound_writes(),
	    .learn = &gui::shell_host::learn_sink(),
	};
	return ports;
}

} // namespace deluge::scenes::menu

namespace deluge::gui::shell_host {

bool applied(edits::Edit& edit) {
	edits::Context ctx{*currentSong, currentSong->project};
	return edits::apply(ctx, edit) == Error::NONE;
}

} // namespace deluge::gui::shell_host
