#include "gui/ui/shell_host/shell_host.h"

#include "board_config.h"
#include "model/edits/edit.h"
#include "model/song/song.h"
#include "scenes/menu/bind_host.h"
#include "scenes/overlay/count_in_scene.h"
#include "scenes/overlay/screensaver_scene.h"

namespace deluge::scenes::menu {

const bind::Ports& host_ports() {
	// Only the UI tier reads it, so it sits in SDRAM, off the audio tier's SRAM budget.
	PLACE_SDRAM_BSS static const bind::Ports ports{
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
	    .midi = &gui::shell_host::midi_devices(),
	    .writes = &gui::shell_host::edit_sink(),
	    .controls_sink = &gui::shell_host::edit_sink(),
	    .device = &gui::shell_host::device_settings(),
	    .device_actions = &gui::shell_host::device_actions(),
	    .midi_feedback = &gui::shell_host::midi_feedback(),
	    .transport = &gui::shell_host::transport(),
	    .transport_actions = &gui::shell_host::transport_actions(),
	};
	return ports;
}

} // namespace deluge::scenes::menu

namespace deluge::gui::shell_host {

void install_scene_ports() {
	const scenes::bind::Ports& ports = scenes::menu::host_ports();
	scenes::overlay::count_in_scene().set_transport(ports.transport);
	scenes::overlay::screensaver_scene().set_device(ports.device);
}

bool applied(edits::Edit& edit) {
	edits::Context ctx{*currentSong, currentSong->project};
	return edits::apply(ctx, edit) == Error::NONE;
}

} // namespace deluge::gui::shell_host
