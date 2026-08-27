#include "gui/ui/notify_drain.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "hid/display/display.h"
#include "util/notify.h"

void drain_notifications() {
	using namespace deluge::notify;
	const DirtySet d = dirty().drain();
	if (d.any()) {
		const LegacyRedraw r = legacy_redraw_for(d);
		if (RootUI* root = getRootUI(); root && (r.main_rows || r.sidebar_rows)) {
			uiNeedsRendering(root, r.main_rows ? 0xFFFFFFFF : 0, r.sidebar_rows ? 0xFFFFFFFF : 0);
		}
		if (r.oled) {
			renderUIsForOled();
		}
	}
	Notice n;
	while (notices().pop(n)) {
		switch (n.kind) {
		case Notice::Kind::Error:
			display->displayError(error_of(n));
			break;
		case Notice::Kind::Text:
			if (n.string_id) {
				display->displayPopup(deluge::l10n::get(string_of(n)), 3, false, 255, 1,
				                      static_cast<PopupType>(n.popup_type));
			}
			else {
				display->displayPopup(n.text, 3, false, 255, 1, static_cast<PopupType>(n.popup_type));
			}
			break;
		case Notice::Kind::Console:
			display->consoleText(n.string_id ? deluge::l10n::get(string_of(n)) : n.text);
			break;
		case Notice::Kind::Working:
			display->displayLoadingAnimationText(n.text);
			break;
		case Notice::Kind::ClearWorking:
			display->removeLoadingAnimation();
			display->removeWorkingAnimation();
			break;
		case Notice::Kind::CancelPopup:
			display->cancelPopup();
			break;
		}
	}
}
