// Differential fidelity harness for ValueTreeDeserializer's XML replay mode (deserializer step 2a).
//
// Ground truth is the real XMLDeserializer reading the fragment's bytes. For each fragment a fixed
// *script* -- the exact call sequence readFromFile would issue for that shape -- is run against both
//   1. XMLDeserializer{fragmentBytes}, and
//   2. ValueTreeDeserializer{*parseXml(fragmentBytes), SourceFormat::Xml},
// and the two return-transcripts must match token-for-token. XML mode is developed by making these
// pass. This links only in the host-sim environment (XMLDeserializer is not available to CppSpec).
//
// Build+run:  cmake --build build-sim --target xml_replay_diff && build-sim/xml_replay_diff

#include "storage/parse_xml.h"
#include "storage/storage_manager.h"
#include "storage/value_tree_deserializer.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using deluge::storage::parseXml;
using deluge::storage::SourceFormat;
using deluge::storage::ValueTreeDeserializer;

namespace {

int failures = 0;
using Transcript = std::vector<std::string>;

// Transcript-append helpers, so each script reads as the readFromFile call sequence it mirrors.
void name(Deserializer& r, Transcript& t) {
	t.push_back(std::string("name=") + r.readNextTagOrAttributeName());
}
void vInt(Deserializer& r, Transcript& t) {
	t.push_back(std::string("int=") + std::to_string(r.readTagOrAttributeValueInt()));
}
void vStr(Deserializer& r, Transcript& t) {
	t.push_back(std::string("str=") + r.readTagOrAttributeValue());
}
void vHex(Deserializer& r, Transcript& t) {
	t.push_back(std::string("hex=") + std::to_string(r.readTagOrAttributeValueHex(-1)));
}
void m(Deserializer& r, Transcript& t, char ch) {
	t.push_back(std::string("match(") + ch + ")=" + (r.match(ch) ? "1" : "0"));
}
void exit_(Deserializer& r, Transcript& t, char const* tag = nullptr, bool closeObject = false) {
	r.exitTag(tag, closeObject);
	t.push_back("exitTag");
}

template <typename Script>
void diff(const char* label, const char* xml, Script script) {
	Transcript a;
	Transcript b;

	// XMLDeserializer null-terminates tokens in place, so it needs a mutable, NUL-padded copy.
	std::vector<char> buf(xml, xml + std::strlen(xml));
	buf.push_back('\0');
	{
		XMLDeserializer x(reinterpret_cast<uint8_t*>(buf.data()), static_cast<size_t>(std::strlen(xml)));
		script(static_cast<Deserializer&>(x), a);
	}

	auto tree = parseXml(xml);
	if (!tree) {
		printf("FAIL %s: parseXml returned nullopt\n", label);
		failures++;
		return;
	}
	{
		ValueTreeDeserializer v(*tree, SourceFormat::Xml);
		script(static_cast<Deserializer&>(v), b);
	}

	if (a != b) {
		failures++;
		printf("FAIL %s: transcripts differ (xml=%zu adapter=%zu)\n", label, a.size(), b.size());
		for (size_t i = 0; i < a.size() || i < b.size(); i++) {
			printf("  [%zu] xml=%-24s | adapter=%s\n", i, i < a.size() ? a[i].c_str() : "<end>",
			       i < b.size() ? b[i].c_str() : "<end>");
		}
	}
	else {
		printf("ok   %s (%zu tokens)\n", label, a.size());
	}
}

} // namespace

int main() {
	// (1) A single attribute-only, self-closing element. Two attributes read back-to-back (no exitTag
	// between them -- legal for attributes, whose value-read leaves the cursor at the next attribute).
	diff("attr-only", R"(<clip section="0" length="384"/>)", [](Deserializer& r, Transcript& t) {
		name(r, t);   // "clip"
		m(r, t, '{'); // enter clip (permissive)
		name(r, t);   // "section"
		vInt(r, t);   // 0
		name(r, t);   // "length"
		vInt(r, t);   // 384
		name(r, t);   // "" (end of clip's attributes)
		exit_(r, t, nullptr, true);
	});

	// (2) A structured child element (implicit descent): entering <modKnobs> is done purely by
	// continuing to read names -- no match('{') is required to descend into it.
	diff("nested-elem", R"(<clip><modKnobs a="1" b="2"/></clip>)", [](Deserializer& r, Transcript& t) {
		name(r, t);   // "clip"
		m(r, t, '{'); // enter clip
		name(r, t);   // "modKnobs" (structured child, staged pending)
		m(r, t, '{'); // permissive; the real descent happens on the next readNext
		name(r, t);   // "a" (implicit descent into modKnobs)
		vInt(r, t);   // 1
		name(r, t);   // "b"
		vInt(r, t);   // 2
		name(r, t);   // "" (end of modKnobs)
		m(r, t, '}');
		exit_(r, t); // finish modKnobs -> back to clip level
		name(r, t);  // "" (end of clip)
		m(r, t, '}');
	});

	// (3) A text-content element (<name>hello</name>) -- a Scalar entry read as a value. Requires an
	// exitTag to skip the close tag before the next sibling (unlike an attribute).
	diff("text-content", R"(<clip><name>hello</name><inKeyMode>1</inKeyMode></clip>)",
	     [](Deserializer& r, Transcript& t) {
		     name(r, t); // "clip"
		     m(r, t, '{');
		     name(r, t); // "name" (descend into clip)
		     vStr(r, t); // "hello"
		     exit_(r, t, "name");
		     name(r, t); // "inKeyMode"
		     vInt(r, t); // 1
		     exit_(r, t, "inKeyMode");
		     name(r, t); // "" (end of clip)
		     m(r, t, '}');
	     });

	// (4) A container of repeated child elements, walked with the array-of-boxes idiom
	//   while (match('{') && *(name = readNext())) { ...; match('}'); exitTag(nullptr, true); }
	// that appears throughout readFromFile (e.g. noteRows). Repeated child names produce repeated
	// ordered entries; there is no Array type.
	diff("repeated-children", R"(<clip><noteRows><noteRow y="0"/><noteRow y="1"/><noteRow y="2"/></noteRows></clip>)",
	     [](Deserializer& r, Transcript& t) {
		     name(r, t); // "clip"
		     m(r, t, '{');
		     name(r, t); // "noteRows"
		     m(r, t, '[');
		     for (int i = 0; i < 4; i++) { // one past the 3 children: last iteration reads ""
			     m(r, t, '{');
			     name(r, t); // "noteRow" x3, then ""
			     if (i < 3) {
				     name(r, t); // "y" (descend into this noteRow)
				     vInt(r, t); // i
				     exit_(r, t, "y");
				     name(r, t); // "" (end of this noteRow)
			     }
			     m(r, t, '}');
			     exit_(r, t, nullptr, true); // leave box
		     }
		     m(r, t, ']');
		     exit_(r, t); // finish noteRows -> back to clip
		     name(r, t);  // "" (end of clip)
		     m(r, t, '}');
	     });

	// (5) Deeply nested structured elements (song>clip>noteRow>note), each entered by implicit
	// descent, then unwound level by level with exitTag.
	diff("deeply-nested", R"(<song><clip><noteRow><note pos="0" length="3"/></noteRow></clip></song>)",
	     [](Deserializer& r, Transcript& t) {
		     name(r, t); // "song"
		     m(r, t, '{');
		     name(r, t); // "clip"
		     m(r, t, '{');
		     name(r, t); // "noteRow"
		     m(r, t, '{');
		     name(r, t); // "note"
		     m(r, t, '{');
		     name(r, t); // "pos"
		     vInt(r, t); // 0
		     name(r, t); // "length"
		     vInt(r, t); // 3
		     name(r, t); // "" end of note
		     m(r, t, '}');
		     exit_(r, t); // finish note
		     name(r, t);  // "" end of noteRow
		     m(r, t, '}');
		     exit_(r, t); // finish noteRow
		     name(r, t);  // "" end of clip
		     m(r, t, '}');
		     exit_(r, t); // finish clip
		     name(r, t);  // "" end of song
		     m(r, t, '}');
	     });

	// (6) Hex values, both as an attribute (0x..) and as a text-content element.
	diff("hex-values", R"(<sample crc="0x7FABCDEF"><loopStart>0x40</loopStart></sample>)",
	     [](Deserializer& r, Transcript& t) {
		     name(r, t); // "sample"
		     m(r, t, '{');
		     name(r, t); // "crc"
		     vHex(r, t); // 0x7FABCDEF
		     exit_(r, t, "crc");
		     name(r, t); // "loopStart"
		     vHex(r, t); // 0x40
		     exit_(r, t, "loopStart");
		     name(r, t); // "" end of sample
		     m(r, t, '}');
	     });

	// (7) A single exitTag that unwinds 2+ nesting levels at once, followed by a REAL intermediate
	// sibling (not a terminating ""). This is the canonical readFromFile "skip an unrecognised child"
	// pattern: <b> is named but never descended into, so one exitTag must discard its whole subtree
	// (<b> AND the nested <c v="9"/> -- two levels) and land back at <a>'s level, where the next child
	// <d y="2"/> is then read for real. This is exactly where a frame-accounting off-by-one would hide.
	// (Note: the reverse -- descend a->b->c then one exitTag *back up* to a -- does NOT arise: caller
	// depth tracks the descent 1:1, so a post-descent exitTag reconciles at most one level, yielding ""
	// for the parent rather than a sibling. The forward multi-level skip below is the real stress.)
	diff("multi-level-skip", R"(<a><b><c v="9"/></b><d y="2"/></a>)", [](Deserializer& r, Transcript& t) {
		name(r, t);       // "a"
		m(r, t, '{');     // enter a
		name(r, t);       // "b" (unwanted child: skip its whole subtree, un-descended)
		exit_(r, t, "b"); // ONE exitTag unwinds b AND nested c back to a's level
		name(r, t);       // "d" <-- REAL intermediate sibling after the 2-level skip
		m(r, t, '{');     // enter d
		name(r, t);       // "y"
		vInt(r, t);       // 2
		exit_(r, t, "y");
		name(r, t); // "" (end of d)
		m(r, t, '}');
		exit_(r, t, "d");
		name(r, t); // "" (end of a)
		m(r, t, '}');
	});

	// (8) An EMPTY container written open+close (not self-closing) -- how Deluge serializes an empty
	// array, e.g. <sessionClips></sessionClips>. parseXml maps it to an empty Map, so the array-of-
	// boxes idiom descends into it and immediately reads "" (loop exits), then a REAL following sibling
	// (<tempo>) is read. If the empty element were a Scalar, the loop's readNext would leak the parent's
	// next sibling instead of "" -- the exact fidelity bug this pins.
	diff("empty-container", R"(<song><clips></clips><tempo v="120"/></song>)", [](Deserializer& r, Transcript& t) {
		name(r, t);   // "song"
		m(r, t, '{'); // enter song
		name(r, t);   // "clips" (empty container, staged pending)
		m(r, t, '[');
		while (true) { // array-of-boxes walk: exits immediately on the empty container
			if (!r.match('{')) {
				t.push_back("match({)=0");
				break;
			}
			t.push_back("match({)=1");
			std::string n = r.readNextTagOrAttributeName();
			t.push_back("name=" + n);
			if (n.empty()) {
				break; // empty container -> "" -> loop exits, matching XMLDeserializer
			}
			m(r, t, '}');
			exit_(r, t, nullptr, true);
		}
		m(r, t, ']');
		exit_(r, t, "clips"); // finish clips -> back to song level
		name(r, t);           // "tempo" <-- REAL sibling after the empty container
		m(r, t, '{');
		name(r, t); // "v"
		vInt(r, t); // 120
		exit_(r, t, "v");
		name(r, t); // "" end of tempo
		m(r, t, '}');
		exit_(r, t, "tempo");
		name(r, t); // "" end of song
		m(r, t, '}');
	});

	// (9) An EMPTY VALUE element written open+close (<name></name>): read as a string value, which
	// must come back "" (parseXml maps it to an empty Map; the value-read methods serve "" via
	// pendingScalar()'s non-Scalar guard, matching XMLDeserializer reading empty text). Then the real
	// following sibling <x v="2"/> is read to confirm the cursor advanced correctly past the empty one.
	diff("empty-value", R"(<clip><name></name><x v="2"/></clip>)", [](Deserializer& r, Transcript& t) {
		name(r, t);   // "clip"
		m(r, t, '{'); // enter clip
		name(r, t);   // "name" (empty value element)
		vStr(r, t);   // "" <-- empty-value read must still return ""
		exit_(r, t, "name");
		name(r, t); // "x" <-- REAL sibling after the empty value
		m(r, t, '{');
		name(r, t); // "v"
		vInt(r, t); // 2
		exit_(r, t, "v");
		name(r, t); // "" end of x
		m(r, t, '}');
		exit_(r, t, "x");
		name(r, t); // "" end of clip
		m(r, t, '}');
	});

	// (10) NUMERIC attribute names. The Deluge format uses digit-started names (e.g. <ccLabels 0="" 2=""
	// 127=""/> for MIDI CC labels). XMLDeserializer's name reader accepts any non-delimiter char, so
	// parseXml must too -- an alpha-only isNameStartChar rejects these and fails the whole parse.
	diff("numeric-attr-names", R"(<ccLabels 0="a" 2="b" 127="c"/>)", [](Deserializer& r, Transcript& t) {
		name(r, t);   // "ccLabels"
		m(r, t, '{'); // enter
		name(r, t);   // "0"
		vStr(r, t);   // "a"
		name(r, t);   // "2"
		vStr(r, t);   // "b"
		name(r, t);   // "127"
		vStr(r, t);   // "c"
		name(r, t);   // "" end of attributes
		exit_(r, t, nullptr, true);
	});

	printf(failures ? "XML_REPLAY_DIFF: %d FAIL\n" : "XML_REPLAY_DIFF: OK\n", failures);
	return failures ? 1 : 0;
}
