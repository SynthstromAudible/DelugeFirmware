#include "gui/ui/browser/folder_import.h"
#include "gui/ui/browser/sample_claim.h"
#include "gui/ui/load/output_replacement.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_slice_ops.h"

namespace deluge::gui::shell_host {
namespace {

class HostCardFacts final : public scenes::bind::CardFacts {
public:
	[[nodiscard]] bool sample_load_in_flight() const override {
		return sampleClaim.explicitInFlight() || folderImport.inFlight() || ui::shellSlicerBusy();
	}
	[[nodiscard]] bool output_may_be_replaced() const override { return anOutputMayBeReplaced(); }
};

} // namespace

const scenes::bind::CardFacts& card_facts() {
	static const HostCardFacts facts;
	return facts;
}

} // namespace deluge::gui::shell_host
