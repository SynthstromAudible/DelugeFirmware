#include "gui/ui/shell_host/shell_host.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"

#include <string>
#include <string_view>

namespace deluge::gui::shell_host {
namespace {

/// The card's file actions, each the legacy step it names.
class HostCardActions final : public deluge::ui::scenes::bind::CardActions {
public:
	[[nodiscard]] bool release_sample_file(std::string_view path) override {
		// `SampleBrowser::buttonAction`'s Shift+SAVE (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`): the
		// preview stops either way, then the file leaves memory unless the song holds it.
		AudioEngine::stopAnyPreviewing();
		return audioFileManager.tryToDeleteAudioFileFromMemoryIfItExists(std::string{path}.c_str());
	}
};

} // namespace

deluge::ui::scenes::bind::CardActions& card_actions() {
	static HostCardActions actions;
	return actions;
}

} // namespace deluge::gui::shell_host
