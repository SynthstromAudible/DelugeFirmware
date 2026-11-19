#include "gui/ui/shell_host/shell_host.h"
#include "scenes/menu/bind_host.h"

namespace deluge::scenes::menu {

const bind::Ports& host_ports() {
	static const bind::Ports ports{
	    .audio = &gui::shell_host::audio_facts(),
	    .card = &gui::shell_host::card_facts(),
	    .ui = &gui::shell_host::ui_facts(),
	};
	return ports;
}

} // namespace deluge::scenes::menu
