#include "storage/value.h"
#include "storage/value_tree_deserializer.h"
#include <cppspec.hpp>
#include <string>

// Coarse mechanics guard for ValueTreeDeserializer's XML replay mode (SourceFormat::Xml) over a
// hand-built, all-ordered-maps tree of the kind parseXml() produces. Ground-truth fidelity against
// the real XMLDeserializer lives in the host-sim differential harness (tests/golden/xml_replay_diff.cpp,
// which cannot link into CppSpec); this just pins the tree-walk mechanics in the fast suite.

// clang-format off
describe value_tree_deserializer_xml("ValueTreeDeserializer (XML mode)", ${
	using deluge::storage::SourceFormat;
	using deluge::storage::Value;
	using deluge::storage::ValueTreeDeserializer;

	// XML mode reads a frame's entries directly from the first call (no leading match('{') needed),
	// because the document root is itself a Map, and returns "" at frame end.
	it("walks a frame's entries and returns empty at the end", _{
		Value doc = Value::makeMap();
		doc.set("id", Value::makeScalar("7"));
		doc.set("section", Value::makeScalar("2"));
		ValueTreeDeserializer d{doc, SourceFormat::Xml};
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("id"));
		expect(d.readTagOrAttributeValueInt()).to_equal(7);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("section"));
		expect(d.readTagOrAttributeValueInt()).to_equal(2);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string(""));
	});

	// match(*) is an unconditional no-op in XML mode, mirroring XMLDeserializer::match: it never
	// descends, pops, or consumes, so the entry read afterwards is unchanged.
	it("match(*) is permissive and never consumes", _{
		Value doc = Value::makeMap();
		doc.set("id", Value::makeScalar("7"));
		ValueTreeDeserializer d{doc, SourceFormat::Xml};
		expect(d.match('{')).to_be_true();
		expect(d.match('}')).to_be_true();
		expect(d.match('[')).to_be_true();
		expect(d.match(']')).to_be_true();
		// None of the matches moved the cursor:
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("id"));
		expect(d.readTagOrAttributeValueInt()).to_equal(7);
	});

	// A structured child element is entered implicitly -- reading its name stages it, and the NEXT
	// readNextTagOrAttributeName() descends into it (no match('{') required), exactly as XML "enters"
	// a child element by continuing to read.
	it("implicitly descends into a child Map and unwinds via exitTag", _{
		Value doc = Value::makeMap();
		Value clip = Value::makeMap();
		clip.set("section", Value::makeScalar("2"));
		clip.set("length", Value::makeScalar("384"));
		doc.set("clip", std::move(clip));
		ValueTreeDeserializer d{doc, SourceFormat::Xml};
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("clip"));
		d.match('{'); // permissive; the descent happens on the next read
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("section"));
		expect(d.readTagOrAttributeValueInt()).to_equal(2);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("length"));
		expect(d.readTagOrAttributeValueInt()).to_equal(384);
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("")); // end of clip
		d.exitTag();                                                                   // back to doc level
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("")); // end of doc
	});

	// Repeated child names produce repeated ordered entries (there is no Array type); the array-of-
	// boxes idiom used across readFromFile visits every one.
	it("iterates every repeated child entry", _{
		Value doc = Value::makeMap();
		Value rows = Value::makeMap();
		rows.set("noteRow", Value::makeScalar("0"));
		rows.set("noteRow", Value::makeScalar("1"));
		rows.set("noteRow", Value::makeScalar("2"));
		doc.set("noteRows", std::move(rows));
		ValueTreeDeserializer d{doc, SourceFormat::Xml};
		expect(std::string(d.readNextTagOrAttributeName())).to_equal(std::string("noteRows"));
		d.match('{');
		int32_t count = 0;
		int32_t sum = 0;
		const char* tag = nullptr;
		while (*(tag = d.readNextTagOrAttributeName())) {
			expect(std::string(tag)).to_equal(std::string("noteRow"));
			sum += d.readTagOrAttributeValueInt();
			count++;
			d.exitTag(tag);
		}
		expect(count).to_equal(3);
		expect(sum).to_equal(3); // 0 + 1 + 2
	});
});
// clang-format on

CPPSPEC_SPEC(value_tree_deserializer_xml);
