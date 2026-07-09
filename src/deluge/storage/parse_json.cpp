#include "storage/parse_json.h"

#include <cctype>
#include <charconv>
#include <cstdint>

namespace deluge::storage {

namespace {

/// Minimal recursive-descent JSON parser. All failure paths return
/// std::nullopt (or false, for the string helper) rather than throwing --
/// malformed input is an expected, common outcome for this parser, not an
/// exceptional one.
class Parser {
public:
	explicit Parser(std::string_view json) : text_(json) {}

	[[nodiscard]] std::optional<Value> parseDocument() {
		skipWhitespace();
		auto value = parseValue();
		if (!value) {
			return std::nullopt;
		}
		skipWhitespace();
		if (pos_ != text_.size()) {
			return std::nullopt; // trailing garbage
		}
		return value;
	}

private:
	std::string_view text_;
	size_t pos_ = 0;

	[[nodiscard]] bool atEnd() const { return pos_ >= text_.size(); }
	[[nodiscard]] char peek() const { return text_[pos_]; }

	void skipWhitespace() {
		while (!atEnd()) {
			switch (peek()) {
			case ' ':
			case '\t':
			case '\n':
			case '\r':
				++pos_;
				break;
			default:
				return;
			}
		}
	}

	// Consumes `c` if it's next in the stream; returns false (without
	// consuming) otherwise.
	[[nodiscard]] bool consume(char c) {
		if (atEnd() || peek() != c) {
			return false;
		}
		++pos_;
		return true;
	}

	[[nodiscard]] std::optional<Value> parseValue() {
		if (atEnd()) {
			return std::nullopt;
		}
		switch (peek()) {
		case '{':
			return parseObject();
		case '[':
			return parseArray();
		case '"':
			return parseStringValue();
		default:
			return parseLiteralOrNumber();
		}
	}

	[[nodiscard]] std::optional<Value> parseObject() {
		if (!consume('{')) {
			return std::nullopt;
		}
		Value obj = Value::makeMap();
		skipWhitespace();
		if (consume('}')) {
			return obj;
		}
		while (true) {
			skipWhitespace();
			if (atEnd() || peek() != '"') {
				return std::nullopt; // keys must be quoted strings
			}
			auto key = parseQuotedString();
			if (!key) {
				return std::nullopt;
			}
			skipWhitespace();
			if (!consume(':')) {
				return std::nullopt;
			}
			skipWhitespace();
			auto value = parseValue();
			if (!value) {
				return std::nullopt;
			}
			obj.set(std::move(*key), std::move(*value));
			skipWhitespace();
			if (consume(',')) {
				continue;
			}
			if (consume('}')) {
				break;
			}
			return std::nullopt; // missing ',' or '}'
		}
		return obj;
	}

	[[nodiscard]] std::optional<Value> parseArray() {
		if (!consume('[')) {
			return std::nullopt;
		}
		Value arr = Value::makeArray();
		skipWhitespace();
		if (consume(']')) {
			return arr;
		}
		while (true) {
			skipWhitespace();
			auto value = parseValue();
			if (!value) {
				return std::nullopt;
			}
			arr.append(std::move(*value));
			skipWhitespace();
			if (consume(',')) {
				continue;
			}
			if (consume(']')) {
				break;
			}
			return std::nullopt; // missing ',' or ']'
		}
		return arr;
	}

	[[nodiscard]] std::optional<Value> parseStringValue() {
		auto s = parseQuotedString();
		if (!s) {
			return std::nullopt;
		}
		return Value::makeScalar(std::move(*s));
	}

	// Appends the UTF-8 encoding of `codepoint` to `out`.
	static void appendUtf8(std::string& out, uint32_t codepoint) {
		if (codepoint <= 0x7F) {
			out.push_back(static_cast<char>(codepoint));
		}
		else if (codepoint <= 0x7FF) {
			out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
		else if (codepoint <= 0xFFFF) {
			out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
		else {
			out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
	}

	// Parses exactly 4 hex digits starting at pos_, advancing past them.
	[[nodiscard]] std::optional<uint32_t> parseHex4() {
		if (pos_ + 4 > text_.size()) {
			return std::nullopt;
		}
		uint32_t value = 0;
		auto [ptr, ec] = std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, value, 16);
		if (ec != std::errc{} || ptr != text_.data() + pos_ + 4) {
			return std::nullopt;
		}
		pos_ += 4;
		return value;
	}

	// Parses a JSON string literal (including surrounding quotes) and returns
	// its unescaped contents, without the quotes.
	[[nodiscard]] std::optional<std::string> parseQuotedString() {
		if (!consume('"')) {
			return std::nullopt;
		}
		std::string out;
		while (true) {
			if (atEnd()) {
				return std::nullopt; // unterminated string
			}
			char c = peek();
			if (c == '"') {
				++pos_;
				return out;
			}
			if (static_cast<unsigned char>(c) < 0x20) {
				return std::nullopt; // raw control character, must be escaped
			}
			if (c != '\\') {
				out.push_back(c);
				++pos_;
				continue;
			}
			// Escape sequence.
			++pos_;
			if (atEnd()) {
				return std::nullopt;
			}
			char esc = peek();
			switch (esc) {
			case '"':
				out.push_back('"');
				++pos_;
				break;
			case '\\':
				out.push_back('\\');
				++pos_;
				break;
			case '/':
				out.push_back('/');
				++pos_;
				break;
			case 'b':
				out.push_back('\b');
				++pos_;
				break;
			case 'f':
				out.push_back('\f');
				++pos_;
				break;
			case 'n':
				out.push_back('\n');
				++pos_;
				break;
			case 'r':
				out.push_back('\r');
				++pos_;
				break;
			case 't':
				out.push_back('\t');
				++pos_;
				break;
			case 'u': {
				++pos_;
				auto hi = parseHex4();
				if (!hi) {
					return std::nullopt;
				}
				uint32_t codepoint = *hi;
				// Combine a UTF-16 surrogate pair into one codepoint if present.
				if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
					if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
						return std::nullopt; // lone high surrogate
					}
					pos_ += 2;
					auto lo = parseHex4();
					if (!lo || *lo < 0xDC00 || *lo > 0xDFFF) {
						return std::nullopt; // missing/invalid low surrogate
					}
					codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (*lo - 0xDC00);
				}
				else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
					return std::nullopt; // lone low surrogate
				}
				appendUtf8(out, codepoint);
				break;
			}
			default:
				return std::nullopt; // invalid escape
			}
		}
	}

	// Parses `true`/`false`/`null`, or a JSON number, storing the raw token
	// text verbatim (no escaping applies to these forms).
	[[nodiscard]] std::optional<Value> parseLiteralOrNumber() {
		static constexpr std::string_view kTrue = "true";
		static constexpr std::string_view kFalse = "false";
		static constexpr std::string_view kNull = "null";
		for (std::string_view keyword : {kTrue, kFalse, kNull}) {
			if (text_.substr(pos_, keyword.size()) == keyword) {
				pos_ += keyword.size();
				return Value::makeScalar(std::string(keyword));
			}
		}
		return parseNumber();
	}

	[[nodiscard]] std::optional<Value> parseNumber() {
		size_t start = pos_;
		if (!atEnd() && peek() == '-') {
			++pos_;
		}
		if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek()))) {
			return std::nullopt;
		}
		if (peek() == '0') {
			++pos_;
		}
		else {
			while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
				++pos_;
			}
		}
		if (!atEnd() && peek() == '.') {
			++pos_;
			if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek()))) {
				return std::nullopt; // digits required after decimal point
			}
			while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
				++pos_;
			}
		}
		if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
			++pos_;
			if (!atEnd() && (peek() == '+' || peek() == '-')) {
				++pos_;
			}
			if (atEnd() || !std::isdigit(static_cast<unsigned char>(peek()))) {
				return std::nullopt; // digits required in exponent
			}
			while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
				++pos_;
			}
		}
		return Value::makeScalar(std::string(text_.substr(start, pos_ - start)));
	}
};

} // namespace

std::optional<Value> parseJson(std::string_view json) {
	Parser parser(json);
	return parser.parseDocument();
}

} // namespace deluge::storage
