#include "gui/ui/shell_host/shell_host.h"
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
	};
	return ports;
}

} // namespace deluge::scenes::menu
