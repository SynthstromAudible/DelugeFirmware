#include "scenes/bind/dispatch.h"

#include "engine-api/types/engine_id.h"
#include "engine-api/types/setting.h"
#include "engines/deluge/settings.h"
#include "gui/l10n/setting_names.h"
#include "scenes/bind/setting_context.h"
#include "scenes/bind/slot_reader.h"

#include <array>
#include <cctype>
#include <tuple>

namespace deluge::scenes::bind {

namespace {

/// A choice list holds at most this many entries: the most choices any row has, plus a stale one.
constexpr size_t kMaxEntries = 32;

/// A target resolved against the song: its row, its reader and its context.
struct Resolved {
	const engine::SettingInfo* row;
	SlotReader reader;
	engine::SettingContext context;
};

std::optional<Resolved> resolve(const Target& target, const project::Song& model, const Addresses& where,
                                const RuntimeFacts& facts) {
	if (target.kind != Target::Kind::SOUND || !where.slot) {
		return std::nullopt;
	}
	const engine::SettingInfo* row = engine::find_setting(instrument::kSettings, target.path);
	SlotReader reader{model, *where.slot, facts};
	if (row == nullptr || !reader.resolved()) {
		return std::nullopt;
	}
	return Resolved{row, reader, setting_context(model, *where.slot, facts)};
}

/// A CHOICE row's listed entries: the menu values whose choice applies, in menu order, with the stored one inserted
/// where it falls when it does not apply.
struct Entries {
	std::array<int32_t, kMaxEntries> menu{}; ///< each entry's menu value in the descriptor's menu space
	size_t count{0};
	std::optional<size_t> stale; ///< the stored choice's entry, when it no longer applies
	size_t current{0};           ///< the stored choice's entry
};

Entries entries_of(const Resolved& resolved, int32_t stored) {
	const engine::SettingInfo& row = *resolved.row;
	const engine::SettingReader reader = resolved.reader.reader();
	const int32_t stored_menu = engine::to_menu(row, stored);
	const auto [least, greatest] = engine::menu_range(row);
	Entries entries;
	bool placed = false;
	for (int32_t menu = least; menu <= greatest && entries.count < kMaxEntries; ++menu) {
		const auto choice = engine::from_menu(row, menu);
		if (!choice) {
			continue;
		}
		const bool applies =
		    engine::setting_choice_applies(row, resolved.context, reader, static_cast<size_t>(choice->first));
		if (menu == stored_menu) {
			entries.current = entries.count;
			placed = true;
			if (!applies) {
				entries.stale = entries.count;
			}
			entries.menu[entries.count++] = menu;
		}
		else if (applies) {
			entries.menu[entries.count++] = menu;
		}
	}
	if (!placed && entries.count < kMaxEntries) { // a stored value no menu value reaches
		entries.stale = entries.current = entries.count;
		entries.menu[entries.count++] = stored_menu;
	}
	return entries;
}

std::optional<int32_t> partner_stored(const Resolved& resolved) {
	const std::string_view partner = engine::mapping_of(*resolved.row).partner;
	return partner.empty() ? std::optional<int32_t>{0} : resolved.reader.read(partner);
}

} // namespace

std::optional<View> view(const Target& target, const project::Song& model, const Addresses& where,
                         const RuntimeFacts& facts) {
	const std::optional<Resolved> resolved = resolve(target, model, where, facts);
	if (!resolved) {
		return std::nullopt;
	}
	const engine::SettingInfo& row = *resolved->row;
	const std::optional<int32_t> stored = resolved->reader.read(row.path);
	const std::optional<int32_t> partner = partner_stored(*resolved);
	if (!stored || !partner) {
		return std::nullopt;
	}
	View shown{.applies = engine::setting_applies(row, resolved->context, resolved->reader.reader())};
	if (row.kind == engine::SettingKind::CHOICE) {
		const Entries entries = entries_of(*resolved, *stored);
		shown.value = static_cast<int32_t>(entries.current);
		shown.greatest = static_cast<int32_t>(entries.count) - 1;
		if (entries.stale) {
			shown.stale = static_cast<int32_t>(*entries.stale);
		}
		return shown;
	}
	shown.value = engine::to_menu(row, *stored, *partner);
	std::tie(shown.least, shown.greatest) = engine::menu_range(row);
	return shown;
}

size_t choice_names(const Target& target, const project::Song& model, const Addresses& where, const RuntimeFacts& facts,
                    std::span<ChoiceName> out) {
	const std::optional<Resolved> resolved = resolve(target, model, where, facts);
	const l10n::SettingNames* names = l10n::setting_names(engine::kDelugeInstrument, target.path);
	if (!resolved || names == nullptr || resolved->row->kind != engine::SettingKind::CHOICE) {
		return 0;
	}
	const std::optional<int32_t> stored = resolved->reader.read(resolved->row->path);
	if (!stored) {
		return 0;
	}
	const Entries entries = entries_of(*resolved, *stored);
	size_t written = 0;
	for (size_t entry = 0; entry < entries.count && written < out.size(); ++entry) {
		const auto choice = engine::from_menu(*resolved->row, entries.menu[entry]);
		const auto index = choice ? static_cast<size_t>(choice->first) : static_cast<size_t>(*stored);
		out[written++] = {.name = index < names->choices.size() ? names->choices[index] : l10n::String::EMPTY_STRING,
		                  .unavailable = entries.stale == entry};
	}
	return written;
}

Title title_of(const Target& target) {
	const l10n::SettingNames* names = l10n::setting_names(engine::kDelugeInstrument, target.path);
	Title title{.title = names != nullptr ? names->title : l10n::String::EMPTY_STRING};
	const std::string_view head = target.path.substr(0, target.path.find('/'));
	int32_t number = 0;
	bool digits = false;
	for (const char c : head) {
		if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
			number = number * 10 + (c - '0');
			digits = true;
		}
	}
	if (digits) {
		title.number = number;
	}
	return title;
}

std::optional<WriteRequest> request_for(const Target& target, const project::Song& model, const Addresses& where,
                                        const RuntimeFacts& facts, int32_t menu) {
	const std::optional<Resolved> resolved = resolve(target, model, where, facts);
	if (!resolved) {
		return std::nullopt;
	}
	const engine::SettingInfo& row = *resolved->row;
	if (row.kind == engine::SettingKind::CHOICE) {
		const std::optional<int32_t> stored = resolved->reader.read(row.path);
		if (!stored) {
			return std::nullopt;
		}
		const Entries entries = entries_of(*resolved, *stored);
		if (menu < 0 || static_cast<size_t>(menu) >= entries.count || entries.stale == static_cast<size_t>(menu)) {
			return std::nullopt;
		}
		return WriteRequest{.target = target, .where = where, .menu_value = entries.menu[static_cast<size_t>(menu)]};
	}
	const auto [least, greatest] = engine::menu_range(row);
	if (menu < least || menu > greatest) {
		return std::nullopt;
	}
	return WriteRequest{.target = target, .where = where, .menu_value = menu};
}

} // namespace deluge::scenes::bind
