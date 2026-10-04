#include "storage/document_format.h"

#include <cppspec.hpp>

using deluge::storage::DocumentFormat;
using deluge::storage::extensionOf;
using deluge::storage::formatOfPath;

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
});

CPPSPEC_SPEC(document_format)
