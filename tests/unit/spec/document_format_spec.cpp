#include "storage/document_format.h"

#include <array>
#include <cppspec.hpp>
#include <string_view>

using deluge::storage::DocumentFormat;
using deluge::storage::extensionOf;
using deluge::storage::formatOfPath;
using deluge::storage::isUpgradedCopy;
using deluge::storage::isUpgradeOf;

// clang-format off
describe document_format("a document's format, from its path", ${
	it("reads a .json file as JSON in any case", _ {
		expect(formatOfPath("SONGS/A.json") == DocumentFormat::JSON).to_equal(true);
		expect(formatOfPath("SONGS/A.JSON") == DocumentFormat::JSON).to_equal(true);
		expect(formatOfPath("SONGS/A.Json") == DocumentFormat::JSON).to_equal(true);
	});

	it("reads everything else as XML", _ {
		expect(formatOfPath("SONGS/A.XML") == DocumentFormat::XML).to_equal(true);
		expect(formatOfPath("SONGS/A.xml") == DocumentFormat::XML).to_equal(true);
		expect(formatOfPath("SONGS/A.json.XML") == DocumentFormat::XML).to_equal(true);
		expect(formatOfPath("SONGS/json") == DocumentFormat::XML).to_equal(true);
		expect(formatOfPath("") == DocumentFormat::XML).to_equal(true);
	});

	it("names each format's extension", _ {
		expect(std::string(extensionOf(DocumentFormat::JSON))).to_equal(std::string(".json"));
		expect(std::string(extensionOf(DocumentFormat::XML))).to_equal(std::string(".XML"));
	});

	it("takes a JSON song as the upgraded copy of the XML song of the same name, in any case", _ {
		expect(isUpgradeOf("A.json", "A.XML")).to_equal(true);
		expect(isUpgradeOf("a.json", "A.xml")).to_equal(true);
		expect(isUpgradeOf("A.json", "B.XML")).to_equal(false);
		expect(isUpgradeOf("A.XML", "A.json")).to_equal(false);
		expect(isUpgradeOf("A.json", "A.json")).to_equal(false);
		expect(isUpgradeOf("AB.json", "A.XML")).to_equal(false);
	});

	it("marks only the JSON song that has an XML save beside it in a listing", _ {
		constexpr std::array<std::string_view, 3> names{"A.json", "A.XML", "B.json"};
		const auto nameAt = [&](size_t i) { return names[i]; };
		expect(isUpgradedCopy(names.size(), 0, nameAt)).to_equal(true);
		expect(isUpgradedCopy(names.size(), 1, nameAt)).to_equal(false);
		expect(isUpgradedCopy(names.size(), 2, nameAt)).to_equal(false);
	});

	it("finds the XML save past other files whose names start the same", _ {
		constexpr std::array<std::string_view, 3> names{"A.json", "A.lead.XML", "A.XML"};
		const auto nameAt = [&](size_t i) { return names[i]; };
		expect(isUpgradedCopy(names.size(), 0, nameAt)).to_equal(true);
	});
});

CPPSPEC_SPEC(document_format)
