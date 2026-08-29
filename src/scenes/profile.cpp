#include "scenes/profile.h"

#include "scenes/l10n_bridge.h"
#include "shell/profiles/deluge.h"

namespace deluge::scenes {

const ui::shell::BoardProfile& deluge_profile() {
	static const ui::shell::BoardProfile profile = [] {
		ui::shell::BoardProfile p = ui::shell::deluge_profile();
		p.text = text_lookup;
		p.error_text = error_lookup;
		return p;
	}();
	return profile;
}

} // namespace deluge::scenes
