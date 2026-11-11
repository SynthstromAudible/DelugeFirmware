/// @file
/// @brief Records what the legacy menu items answer, as the reference tables `tests/golden/ui/menu_oracle/` holds.
///
/// Not a spec of anything: the scenes-spec gate skips `freeze/`, and every example fails unless `MENU_ORACLE_OUT`
/// names the directory to write into. One run writes the one family `MENU_ORACLE_FAMILIES` names, so each family
/// starts in a fresh process, as the spec that reads it does:
///
///     GOLDEN_SCENARIO=scenes_spec SCENES_SPEC=freeze/menu_oracles_spec SCENES_SPEC_CARD=<card> \
///       MENU_ORACLE_FAMILIES=<family> MENU_ORACLE_OUT=<repo>/tests/golden/ui/menu_oracle golden_vt_render
///
/// Each family asks the legacy items the questions its spec asks, in the spec's order and on the spec's own fixtures
/// (the shared `support/*_fixtures.h`), because a write leaves the model where it wrote.
///
/// @note This file may include `gui/menu_item/`, `gui/ui/menus.h` and `gui/ui/sound_editor.h`; the specs that read
///       the tables do not.

#include "support/oracle_table.h"

#include "cppspec.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "spec/support/spec_main.h"

namespace {

using deluge::scenes::spec::oracle_escape;

/// @brief The rows one family writes, in the order it asks them.
class OracleWriter {
public:
	explicit OracleWriter(std::string name) : name_(std::move(name)) {}

	void row(std::string_view path, std::string_view column, std::vector<std::string> values = {}) {
		std::string key{path};
		key += '\t';
		key += column;
		if (!keys_.insert(key).second) {
			duplicates_ += std::string{path} + " " + std::string{column} + "\n";
			return;
		}
		std::string line = oracle_escape(path) + "\t" + oracle_escape(column);
		for (const std::string& v : values) {
			line += "\t" + oracle_escape(v);
		}
		lines_.push_back(std::move(line));
	}
	void row(std::string_view path, std::string_view column, std::string value) {
		row(path, column, std::vector<std::string>{std::move(value)});
	}
	void row(std::string_view path, std::string_view column, int64_t value) {
		row(path, column, std::to_string(value));
	}

	/// @brief The keys written twice, one per line; empty when every key was written once.
	[[nodiscard]] const std::string& duplicates() const { return duplicates_; }
	[[nodiscard]] size_t size() const { return lines_.size(); }

	/// @brief Write `<MENU_ORACLE_OUT>/<name>.tsv`; false when the directory is unset or the file cannot be written.
	bool flush() const {
		const char* dir = std::getenv("MENU_ORACLE_OUT");
		if (dir == nullptr || dir[0] == '\0') {
			return false;
		}
		std::ofstream out(std::string{dir} + "/" + name_ + ".tsv");
		out << "# Reference answers of upstream main's menu, one row per answer: path, column, values "
		       "(tab-separated).\n"
		       "# A row changes only to follow main's source (git show origin/main:<path>); tests/golden/README.md "
		       "says how.\n";
		for (const std::string& line : lines_) {
			out << line << "\n";
		}
		return out.good();
	}

private:
	std::string name_;
	std::set<std::string> keys_;
	std::vector<std::string> lines_;
	std::string duplicates_;
};

/// @brief Whether this run writes @p family: `MENU_ORACLE_FAMILIES` names exactly one.
bool writes(std::string_view family) {
	const char* named = std::getenv("MENU_ORACLE_FAMILIES");
	return named != nullptr && family == named;
}

/// @brief A panel as the tables hold it: `pixels/<name>.pbm`.
bool write_pixels(std::string_view name, const std::string& pbm) {
	const char* dir = std::getenv("MENU_ORACLE_OUT");
	if (dir == nullptr) {
		return false;
	}
	std::filesystem::create_directories(std::string{dir} + "/pixels");
	std::ofstream(std::string{dir} + "/pixels/" + std::string{name} + ".pbm") << pbm;
	return true;
}

} // namespace

// clang-format off
describe freeze_frame("menu oracle freezer: its run", $ {
	it("has a directory to write into and exactly one family to write", _ {
		const char* dir = std::getenv("MENU_ORACLE_OUT");
		expect(dir != nullptr && dir[0] != '\0').to_be_true();
		const char* family = std::getenv("MENU_ORACLE_FAMILIES");
		expect(family != nullptr && std::string_view{family}.find(',') == std::string_view::npos).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(freeze_menu_oracles_spec, freeze_frame /*, one family per later task, appended in task order */)
