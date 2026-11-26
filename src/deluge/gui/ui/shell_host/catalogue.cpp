#include "gui/ui/shell_host/shell_host.h"
#include "storage/file_catalogue.h"
#include "storage/file_item.h"
#include "storage/storage_manager.h"
#include "util/string.h"

#include <cstddef>
#include <new>
#include <string>

namespace deluge::gui::shell_host {
namespace {

using deluge::storage::FileCatalogue;
using deluge::ui::scenes::bind::CatalogueEntry;
using deluge::ui::scenes::bind::FileKinds;
using deluge::ui::scenes::bind::ListingOutcome;
using deluge::ui::scenes::bind::ListingSpec;
using deluge::ui::scenes::bind::ListingStep;
using deluge::ui::scenes::bind::SearchFrom;
using deluge::ui::scenes::bind::SearchHit;

/// @brief `FileKinds::AUDIO`'s extensions, NULL-terminated as `readFileItemsForFolder` takes them.
char const* kAudioExtensions[] = {"WAV", "AIFF", "AIF", nullptr};
/// @brief `FileKinds::SYX`'s extension, NULL-terminated as `readFileItemsForFolder` takes it.
char const* kSyxExtensions[] = {"SYX", nullptr};

/// @return The extension list `readFileItemsForFolder` takes for @p kinds.
char const** extensions_of(FileKinds kinds) {
	switch (kinds) {
	case FileKinds::AUDIO:
		return kAudioExtensions;
	case FileKinds::SYX:
		return kSyxExtensions;
	case FileKinds::XML:
		break;
	}
	return allowedFileExtensionsXML;
}

/// @return The catalogue's search direction for @p from.
int32_t direction_of(SearchFrom from) {
	switch (from) {
	case SearchFrom::LEFT:
		return CATALOG_SEARCH_LEFT;
	case SearchFrom::BOTH:
		return CATALOG_SEARCH_BOTH;
	case SearchFrom::RIGHT:
		break;
	}
	return CATALOG_SEARCH_RIGHT;
}

/// The card's one file catalogue, which this answers for as `FileCatalogue`'s statics.
class HostCatalogue final : public deluge::ui::scenes::bind::Catalogue {
public:
	[[nodiscard]] bool claim(const void* owner) override { return FileCatalogue::claim(owner); }
	void release(const void* owner) override { FileCatalogue::release(owner); }

	[[nodiscard]] ListingOutcome list(const ListingSpec& spec) override {
		ListingOutcome out{};
		const Error error = step(spec, out);
		if (error != Error::NONE) {
			out.error = static_cast<int16_t>(error);
		}
		return out;
	}

	[[nodiscard]] std::string_view dir() const override { return FileCatalogue::currentDir; }
	[[nodiscard]] std::size_t count() const override { return FileCatalogue::fileItems.size(); }

	[[nodiscard]] std::optional<CatalogueEntry> entry(std::size_t index) const override {
		if (index >= FileCatalogue::fileItems.size()) {
			return std::nullopt;
		}
		const FileItem& item = FileCatalogue::fileItems[index];
		return CatalogueEntry{.name = item.filename, .folder = item.isFolder};
	}

	[[nodiscard]] SearchHit search(std::string_view name) override {
		// The scenes search on every keystroke and detent, ticks after the listing that sorted the entries, so the
		// comparator is pinned back to that sort's before the binary search.
		FileCatalogue::repinComparator();
		bool exact = false;
		const int32_t index = FileCatalogue::searchFileItems(std::string{name}.c_str(), &exact);
		return SearchHit{.index = static_cast<std::size_t>(index < 0 ? 0 : index), .exact = exact};
	}

	[[nodiscard]] bool culled_before() const override { return FileCatalogue::numFileItemsDeletedAtStart != 0; }
	[[nodiscard]] bool culled_after() const override { return FileCatalogue::numFileItemsDeletedAtEnd != 0; }

	void erase(std::size_t index) override {
		if (index < FileCatalogue::fileItems.size()) {
			FileCatalogue::fileItems.erase(FileCatalogue::fileItems.begin() + static_cast<std::ptrdiff_t>(index));
		}
	}

	void clear() override { FileCatalogue::emptyFileItems(); }

private:
	/// @brief Move the catalogue's folder as @p spec's step says, then list it.
	static Error step(const ListingSpec& spec, ListingOutcome& out) {
		switch (spec.step) {
		case ListingStep::OPEN:
			if (!spec.path.empty()) {
				FileCatalogue::currentDir = spec.path;
			}
			return list_current_dir(spec, spec.start_at);

		case ListingStep::INTO_FOLDER:
			if (spec.path.empty()) {
				return Error::BUG;
			}
			if (!FileCatalogue::currentDir.empty()) {
				FileCatalogue::currentDir.append("/");
			}
			FileCatalogue::currentDir.append(spec.path);
			return list_current_dir(spec, {});

		case ListingStep::UP_LEVEL: {
			const size_t slash = FileCatalogue::currentDir.rfind('/');
			if (slash == std::string::npos || slash == 0) {
				return Error::NO_FURTHER_DIRECTORY_LEVELS_TO_GO_UP;
			}
			out.entered_text = FileCatalogue::currentDir.substr(slash + 1);
			FileCatalogue::currentDir.resize(slash);
			return list_current_dir(spec, out.entered_text);
		}

		case ListingStep::BY_PATH: {
			if (!StorageManager::fileExists(spec.path.c_str()).value_or(false)) {
				return Error::FILE_NOT_FOUND;
			}
			const size_t slash = spec.path.rfind('/');
			if (slash == std::string::npos) {
				out.entered_text = spec.path;
				FileCatalogue::currentDir.clear();
			}
			else {
				out.entered_text = spec.path.substr(slash + 1);
				FileCatalogue::currentDir = spec.path.substr(0, slash);
			}
			return list_current_dir(spec, out.entered_text);
		}

		case ListingStep::RELOAD:
			return list_current_dir(spec, spec.start_at);
		}
		return Error::BUG;
	}

	/// @brief Read the catalogue's folder from @p start_at, falling back to the default folder, which is made when
	///        it is missing, then sort and drop the shadowed entries.
	static Error list_current_dir(const ListingSpec& spec, std::string_view start_at) {
		const std::string start{start_at};
		char const* const start_name = start.empty() ? nullptr : start.c_str();
		const int32_t direction = spec.search             ? direction_of(*spec.search)
		                          : start_name != nullptr ? CATALOG_SEARCH_BOTH
		                                                  : CATALOG_SEARCH_RIGHT;
		char const** const extensions = extensions_of(spec.kinds);
		const auto read = [&](char const* from, int32_t dir) {
			return FileCatalogue::readFileItemsForFolder(nullptr, spec.allow_folders, extensions, from,
			                                             FILE_ITEMS_MAX_NUM_ELEMENTS, dir, spec.interpret_note_names);
		};

		Error error = read(start_name, direction);
		if (error == Error::FOLDER_DOESNT_EXIST && !spec.default_dir.empty()
		    && !deluge::string::caselessEquals(FileCatalogue::currentDir, spec.default_dir)) {
			FileCatalogue::currentDir = spec.default_dir;
			error = read(nullptr, CATALOG_SEARCH_RIGHT);
		}
		if (error == Error::FOLDER_DOESNT_EXIST && !spec.default_dir.empty()
		    && deluge::string::caselessEquals(FileCatalogue::currentDir, spec.default_dir)) {
			error = FileCatalogue::makeFolder(spec.default_dir);
			if (error == Error::NONE) {
				error = read(nullptr, CATALOG_SEARCH_RIGHT);
			}
		}
		if (error != Error::NONE) {
			return error;
		}
		if (!FileCatalogue::fileItems.empty()) {
			FileCatalogue::sortFileItems(spec.interpret_note_names);
			if (!spec.allow_folders_sharing_name_with_file) {
				FileCatalogue::deleteFolderAndDuplicateItems(Availability::ANY);
			}
		}
		return Error::NONE;
	}
};

} // namespace

deluge::ui::scenes::bind::Catalogue& catalogue() {
	// Never destroyed: a scene's listing seam, itself a static, may hand its claim back from a destructor that runs
	// after this function's statics are gone.
	alignas(HostCatalogue) static std::byte storage[sizeof(HostCatalogue)];
	static HostCatalogue* const catalogue = new (storage) HostCatalogue;
	return *catalogue;
}

} // namespace deluge::gui::shell_host
