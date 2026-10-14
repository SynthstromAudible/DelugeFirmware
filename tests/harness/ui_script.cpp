#include "ui_script.h"

#include <charconv>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace deluge::harness {

namespace {

/// @brief Trim ASCII whitespace (space, tab, `\r`) from both ends.
std::string_view trim(std::string_view s) {
	size_t begin = 0;
	while (begin < s.size() && (s[begin] == ' ' || s[begin] == '\t' || s[begin] == '\r')) {
		begin++;
	}
	size_t end = s.size();
	while (end > begin && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) {
		end--;
	}
	return s.substr(begin, end - begin);
}

/// @brief Split \p s on runs of spaces/tabs into non-empty tokens.
std::vector<std::string_view> tokenize(std::string_view s) {
	std::vector<std::string_view> tokens;
	size_t i = 0;
	while (i < s.size()) {
		while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
			i++;
		}
		size_t start = i;
		while (i < s.size() && s[i] != ' ' && s[i] != '\t') {
			i++;
		}
		if (i > start) {
			tokens.push_back(s.substr(start, i - start));
		}
	}
	return tokens;
}

bool parse_u32(std::string_view s, uint32_t& out) {
	if (s.empty()) {
		return false;
	}
	auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
	return ec == std::errc() && ptr == s.data() + s.size();
}

bool parse_i32(std::string_view s, int32_t& out) {
	if (s.empty()) {
		return false;
	}
	// std::from_chars rejects a leading '+' for integers (only '-' is accepted), but the script
	// format uses "+1"/"-3" for signed encoder detents -- strip it before parsing.
	std::string_view digits = (s[0] == '+') ? s.substr(1) : s;
	if (digits.empty()) {
		return false;
	}
	auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), out);
	return ec == std::errc() && ptr == digits.data() + digits.size();
}

/// @brief Copy \p src into fixed buffer \p dst (size \p n), truncating and NUL-terminating.
void copy_field(char* dst, size_t n, std::string_view src) {
	size_t len = src.size() < n - 1 ? src.size() : n - 1;
	std::memcpy(dst, src.data(), len);
	dst[len] = '\0';
}

void set_error(ScriptResult& r, uint16_t line, std::string_view message) {
	ParseError e{};
	e.line = line;
	copy_field(e.message, sizeof(e.message), message);
	r.error = e;
}

} // namespace

ScriptResult parse_script(std::string_view text) {
	ScriptResult result;
	uint32_t last_at_ms = 0;
	bool have_last = false;
	uint16_t line_no = 0;
	size_t pos = 0;

	while (pos <= text.size()) {
		size_t nl = text.find('\n', pos);
		std::string_view raw_line = (nl == std::string_view::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
		line_no++;

		std::string_view line = raw_line;
		size_t hash = line.find('#');
		if (hash != std::string_view::npos) {
			line = line.substr(0, hash);
		}
		line = trim(line);

		if (!line.empty()) {
			std::vector<std::string_view> tok = tokenize(line);

			uint32_t at_ms = 0;
			if (tok.empty() || !parse_u32(tok[0], at_ms)) {
				set_error(result, line_no, "bad time");
				return result;
			}
			if (have_last && at_ms < last_at_ms) {
				set_error(result, line_no, "time must be non-decreasing");
				return result;
			}
			last_at_ms = at_ms;
			have_last = true;

			if (tok.size() < 2) {
				set_error(result, line_no, "missing command");
				return result;
			}
			std::string_view cmd = tok[1];

			Step step{};
			step.at_ms = at_ms;

			if (cmd == "board") {
				step.kind = StepKind::Board;
				if (tok.size() >= 3) {
					copy_field(step.text, sizeof(step.text), tok[2]);
				}
			}
			else if (cmd == "snapshot") {
				if (tok.size() < 3) {
					set_error(result, line_no, "snapshot needs a name");
					return result;
				}
				step.kind = StepKind::Snapshot;
				copy_field(step.name, sizeof(step.name), tok[2]);
			}
			else if (cmd == "button") {
				if (tok.size() < 4) {
					set_error(result, line_no, "button needs a name and down|up");
					return result;
				}
				step.kind = StepKind::Button;
				copy_field(step.name, sizeof(step.name), tok[2]);
				if (tok[3] == "down") {
					step.down = true;
				}
				else if (tok[3] == "up") {
					step.down = false;
				}
				else {
					set_error(result, line_no, "button requires down|up");
					return result;
				}
			}
			else if (cmd == "encoder") {
				if (tok.size() < 4) {
					set_error(result, line_no, "encoder needs a role and signed detents");
					return result;
				}
				step.kind = StepKind::Encoder;
				copy_field(step.name, sizeof(step.name), tok[2]);
				if (!parse_i32(tok[3], step.a)) {
					set_error(result, line_no, "encoder detents must be a signed integer");
					return result;
				}
			}
			else if (cmd == "pad") {
				if (tok.size() < 5) {
					set_error(result, line_no, "pad needs x y down|up");
					return result;
				}
				step.kind = StepKind::Pad;
				if (!parse_i32(tok[2], step.a) || !parse_i32(tok[3], step.b)) {
					set_error(result, line_no, "pad x y must be integers");
					return result;
				}
				if (tok[4] == "down") {
					step.down = true;
					if (tok.size() >= 6) {
						if (!parse_i32(tok[5], step.c)) {
							set_error(result, line_no, "pad velocity must be an integer");
							return result;
						}
					}
					else {
						step.c = 255; // default velocity
					}
				}
				else if (tok[4] == "up") {
					step.down = false;
					step.c = 0; // pad ... up -> velocity 0
				}
				else {
					set_error(result, line_no, "pad requires down|up");
					return result;
				}
			}
			else if (cmd == "released") {
				step.kind = StepKind::Released;
			}
			else if (cmd == "model") {
				if (tok.size() < 3) {
					set_error(result, line_no, "model needs a Dirty kind");
					return result;
				}
				step.kind = StepKind::Model;
				copy_field(step.name, sizeof(step.name), tok[2]);
				if (tok.size() >= 4) {
					parse_i32(tok[3], step.a); // clip index, if present and numeric
				}
			}
			else if (cmd == "notice") {
				if (tok.size() < 4) {
					set_error(result, line_no, "notice needs a Kind and an arg");
					return result;
				}
				step.kind = StepKind::Notice;
				copy_field(step.name, sizeof(step.name), tok[2]);
				copy_field(step.text, sizeof(step.text), tok[3]);
				parse_i32(tok[3], step.a); // stored numerically too, when the arg is numeric
			}
			else if (cmd == "quit") {
				step.kind = StepKind::Quit;
			}
			else {
				set_error(result, line_no, "unknown command");
				return result;
			}

			if (!result.steps.push_back(step)) {
				set_error(result, line_no, "too many steps");
				return result;
			}
		}

		if (nl == std::string_view::npos) {
			break;
		}
		pos = nl + 1;
	}

	return result;
}

} // namespace deluge::harness

namespace {
deluge::harness::ScriptResult g_script;
size_t g_cursor = 0;
} // namespace

extern "C" int ui_script_load(const char* path) {
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return -1;
	}
	std::ostringstream buf;
	buf << in.rdbuf();
	g_script = deluge::harness::parse_script(buf.str());
	g_cursor = 0;
	if (g_script.error.has_value()) {
		return static_cast<int>(g_script.error->line);
	}
	return 0;
}

extern "C" int ui_script_next_due(uint32_t now_ms, UiScriptStep* out) {
	if (out == nullptr || g_cursor >= g_script.steps.size()) {
		return 0;
	}
	const deluge::harness::Step& step = g_script.steps[g_cursor];
	if (step.at_ms > now_ms) {
		return 0;
	}
	out->at_ms = step.at_ms;
	out->kind = static_cast<uint8_t>(step.kind);
	std::memcpy(out->name, step.name, sizeof(out->name));
	out->a = step.a;
	out->b = step.b;
	out->c = step.c;
	out->down = step.down ? 1 : 0;
	std::memcpy(out->text, step.text, sizeof(out->text));
	g_cursor++;
	return 1;
}

extern "C" void ui_script_rewind() {
	g_cursor = 0;
}
