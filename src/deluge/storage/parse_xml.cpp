#include "storage/parse_xml.h"

#include <array>
#include <cctype>
#include <utility>

namespace deluge::storage {

namespace {

/// Minimal recursive-descent XML parser producing a format-neutral Value
/// tree. All failure paths return std::nullopt rather than throwing --
/// malformed input is an expected, common outcome for this parser, not an
/// exceptional one.
class Parser {
public:
	explicit Parser(std::string_view xml) : text_(xml) {}

	[[nodiscard]] std::optional<Value> parseDocument() {
		skipMisc();
		auto root = parseElement();
		if (!root) {
			return std::nullopt;
		}
		skipMisc();
		if (pos_ != text_.size()) {
			return std::nullopt; // trailing garbage after the root element
		}
		Value doc = Value::makeMap();
		doc.set(std::move(root->first), std::move(root->second));
		return doc;
	}

private:
	std::string_view text_;
	size_t pos_ = 0;

	[[nodiscard]] bool atEnd() const { return pos_ >= text_.size(); }
	[[nodiscard]] char peek() const { return text_[pos_]; }
	[[nodiscard]] bool startsWith(std::string_view s) const { return text_.substr(pos_, s.size()) == s; }

	[[nodiscard]] bool consume(char c) {
		if (atEnd() || peek() != c) {
			return false;
		}
		++pos_;
		return true;
	}

	[[nodiscard]] bool consume(std::string_view s) {
		if (!startsWith(s)) {
			return false;
		}
		pos_ += s.size();
		return true;
	}

	static bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
	// The Deluge format uses digit-started names (e.g. numeric MIDI CC labels: <ccLabels 0="" 2=""/>),
	// which strict XML forbids but XMLDeserializer accepts (its name reader takes any non-delimiter char).
	// Include digits so parseXml stays faithful to the streaming reader.
	static bool isNameStartChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == ':'; }
	static bool isNameChar(char c) {
		return isNameStartChar(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '.';
	}

	void skipWhitespace() {
		while (!atEnd() && isWhitespace(peek())) {
			++pos_;
		}
	}

	// Skips whitespace, the <?xml ... ?> prolog, and <!-- ... --> comments,
	// wherever they may appear between/around elements.
	void skipMisc() {
		while (true) {
			skipWhitespace();
			if (startsWith("<?")) {
				auto end = text_.find("?>", pos_ + 2);
				if (end == std::string_view::npos) {
					pos_ = text_.size(); // let the caller detect the resulting error
					return;
				}
				pos_ = end + 2;
				continue;
			}
			if (startsWith("<!--")) {
				auto end = text_.find("-->", pos_ + 4);
				if (end == std::string_view::npos) {
					pos_ = text_.size();
					return;
				}
				pos_ = end + 3;
				continue;
			}
			return;
		}
	}

	[[nodiscard]] std::optional<std::string> parseName() {
		if (atEnd() || !isNameStartChar(peek())) {
			return std::nullopt;
		}
		size_t start = pos_;
		while (!atEnd() && isNameChar(peek())) {
			++pos_;
		}
		return std::string(text_.substr(start, pos_ - start));
	}

	// Unescapes the five predefined XML entities in `raw`; any other '&' is
	// passed through literally -- this parser does not resolve numeric
	// character references or custom entities, which the Deluge format
	// doesn't use.
	static std::string unescape(std::string_view raw) {
		static constexpr std::array<std::pair<std::string_view, char>, 5> kEntities{{
		    {"&amp;", '&'},
		    {"&lt;", '<'},
		    {"&gt;", '>'},
		    {"&quot;", '"'},
		    {"&apos;", '\''},
		}};
		std::string out;
		out.reserve(raw.size());
		size_t i = 0;
		while (i < raw.size()) {
			if (raw[i] != '&') {
				out.push_back(raw[i]);
				++i;
				continue;
			}
			bool matched = false;
			for (const auto& [entity, ch] : kEntities) {
				if (raw.substr(i, entity.size()) == entity) {
					out.push_back(ch);
					i += entity.size();
					matched = true;
					break;
				}
			}
			if (!matched) {
				out.push_back('&');
				++i;
			}
		}
		return out;
	}

	// Parses a quoted attribute value ("..." or '...'), returning its
	// unescaped contents without the quotes.
	[[nodiscard]] std::optional<std::string> parseAttributeValue() {
		if (atEnd() || (peek() != '"' && peek() != '\'')) {
			return std::nullopt;
		}
		char quote = peek();
		++pos_;
		size_t start = pos_;
		while (!atEnd() && peek() != quote) {
			++pos_;
		}
		if (atEnd()) {
			return std::nullopt; // unterminated attribute value
		}
		std::string raw(text_.substr(start, pos_ - start));
		++pos_; // closing quote
		return unescape(raw);
	}

	// Parses one element (`<name ...>...</name>` or `<name .../>`), returning
	// its tag name and Value. Assumes the caller has already skipped leading
	// whitespace/misc.
	[[nodiscard]] std::optional<std::pair<std::string, Value>> parseElement() {
		if (!consume('<')) {
			return std::nullopt;
		}
		auto name = parseName();
		if (!name) {
			return std::nullopt;
		}
		Value element = Value::makeMap();
		bool hasAttributes = false;
		while (true) {
			skipWhitespace();
			if (atEnd()) {
				return std::nullopt; // unterminated start tag
			}
			if (peek() == '/' || peek() == '>') {
				break;
			}
			auto attrName = parseName();
			if (!attrName) {
				return std::nullopt;
			}
			skipWhitespace();
			if (!consume('=')) {
				return std::nullopt;
			}
			skipWhitespace();
			auto attrValue = parseAttributeValue();
			if (!attrValue) {
				return std::nullopt;
			}
			element.set(std::move(*attrName), Value::makeScalar(std::move(*attrValue)));
			hasAttributes = true;
		}
		if (consume("/>")) {
			return std::make_pair(std::move(*name), std::move(element));
		}
		if (!consume('>')) {
			return std::nullopt; // stray '/' not followed by '>'
		}
		auto body = parseContent(*name, hasAttributes, std::move(element));
		if (!body) {
			return std::nullopt;
		}
		return std::make_pair(std::move(*name), std::move(*body));
	}

	// Parses element content up to and including the matching `</name>`.
	// `element` already holds any attributes parsed from the opening tag,
	// appended in document order; child elements are appended to it the same
	// way. Returns the finished Map (if there were attributes or child
	// elements) or a Scalar of the raw unescaped text (if there were
	// neither) -- the Deluge format never mixes attributes/children with
	// real text on one element, so any non-whitespace text alongside
	// attributes or children is treated as malformed input.
	[[nodiscard]] std::optional<Value> parseContent(const std::string& name, bool hasAttributes, Value element) {
		std::string text;
		bool sawChildElement = false;
		while (true) {
			if (atEnd()) {
				return std::nullopt; // unterminated element
			}
			if (startsWith("</")) {
				pos_ += 2;
				auto closeName = parseName();
				skipWhitespace();
				if (!closeName || *closeName != name || !consume('>')) {
					return std::nullopt; // mismatched or malformed close tag
				}
				break;
			}
			if (startsWith("<!--")) {
				auto end = text_.find("-->", pos_ + 4);
				if (end == std::string_view::npos) {
					return std::nullopt;
				}
				pos_ = end + 3;
				continue;
			}
			if (peek() == '<') {
				auto child = parseElement();
				if (!child) {
					return std::nullopt;
				}
				element.set(std::move(child->first), std::move(child->second));
				sawChildElement = true;
				continue;
			}
			size_t start = pos_;
			while (!atEnd() && peek() != '<') {
				++pos_;
			}
			text += unescape(text_.substr(start, pos_ - start));
		}
		if (hasAttributes || sawChildElement) {
			// Stray non-whitespace text alongside attributes/children is DROPPED, not rejected. Once an
			// element has structure, XMLDeserializer ignores inter-tag text -- readNextTagOrAttributeName
			// does skipUntilChar('<') between tags -- so some (malformed) Deluge files carry ignorable text
			// here, e.g. issue #2514's <kitParams> writes a stray '>' that turns its attributes into text
			// before the real child elements. Matching the streaming reader (drop the text, keep the
			// structure) rather than failing the whole parse keeps load-via-tree faithful on such files.
			return element;
		}
		// No attributes and no child elements. A body with real text is a text-content Scalar
		// (<cc>0</cc>). An empty-or-whitespace-only body is a *truly empty* element and is mapped to an
		// empty Map -- structurally identical to a self-closing <tag/>. This matters because Deluge
		// writes an empty array container as <sessionClips></sessionClips> (open+close, not
		// self-closing): as an empty Map the array-of-boxes replay descends into it and immediately
		// reads "" (loop exits), matching XMLDeserializer; as a Scalar it would wrongly leak the
		// parent's next sibling. An empty *value* element (<clipName></clipName>) is likewise an empty
		// Map, which the value-read methods serve as "" via pendingScalar()'s non-Scalar guard.
		if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
			return element; // empty Map
		}
		return Value::makeScalar(std::move(text));
	}
};

} // namespace

std::optional<Value> parseXml(std::string_view xml) {
	Parser parser(xml);
	return parser.parseDocument();
}

} // namespace deluge::storage
