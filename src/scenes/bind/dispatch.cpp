#include "scenes/bind/dispatch.h"

#include "engine-api/types/engine_id.h"
#include "engine-api/types/setting.h"
#include "engines/deluge/settings.h"
#include "engines/effects/settings.h"
#include "gui/l10n/setting_names.h"
#include "scenes/bind/chain_reader.h"
#include "scenes/bind/model_reader.h"
#include "scenes/bind/setting_context.h"
#include "scenes/bind/slot_reader.h"

#include <array>
#include <tuple>
#include <variant>

namespace deluge::scenes::bind {

namespace {

/// A choice list holds at most this many entries: the most choices any row has, plus a stale one.
constexpr size_t kMaxEntries = 32;

/// A row's held values, read from a sound's slot or a host chain.
class Reader {
public:
	explicit Reader(SlotReader slot) : reader_{slot} {}
	explicit Reader(ChainReader chain) : reader_{chain} {}
	explicit Reader(ModelReader model) : reader_{model} {}
	[[nodiscard]] bool resolved() const {
		return std::visit([](const auto& reader) { return reader.resolved(); }, reader_);
	}
	[[nodiscard]] std::optional<int32_t> read(std::string_view path) const {
		return std::visit([path](const auto& reader) { return reader.read(path); }, reader_);
	}
	/// Valid while this object lives.
	[[nodiscard]] engine::SettingReader reader() const {
		return std::visit([](const auto& reader) { return reader.reader(); }, reader_);
	}

private:
	std::variant<SlotReader, ChainReader, ModelReader> reader_;
};

/// A target resolved against the song: its row, its reader, its context and whose name table names it.
struct Resolved {
	const engine::SettingInfo* row;
	Reader reader;
	engine::SettingContext context;
	l10n::NamesOwner names_owner;
};

/// A target on a model group's record (a MODEL target, or a SOUND target on an audio clip), resolved.
std::optional<Resolved> resolve_model(const Target& target, const project::Song& model, const Addresses& where,
                                      const RuntimeFacts& facts) {
	const std::optional<project::SettingAddress> address = setting_address(target, where);
	if (!address) {
		return std::nullopt;
	}
	const std::string_view path = target.kind == Target::Kind::MODEL ? target.path : target.audio_clip_path;
	const engine::SettingInfo* row = engine::find_setting(project::table_of(address->group), path);
	if (row == nullptr) {
		return std::nullopt;
	}
	Reader reader{ModelReader{model, *address, facts}};
	if (!reader.resolved()) {
		return std::nullopt;
	}
	return Resolved{row, reader, setting_context(model, *address, where.instrument, facts), address->group};
}

std::optional<Resolved> resolve(const Target& target, const project::Song& model, const Addresses& where,
                                const RuntimeFacts& facts) {
	if (target.kind == Target::Kind::MODEL || (target.kind == Target::Kind::SOUND && !where.slot)) {
		return resolve_model(target, model, where, facts);
	}
	const bool effect = target.kind == Target::Kind::EFFECT;
	if (target.kind != Target::Kind::SOUND && !effect) {
		return std::nullopt;
	}
	// A sound's effects are rows of its own table, read and named as its other settings are; a host chain's are named
	// by the host chains' table.
	const engine::SettingInfo* row =
	    engine::find_setting(effect ? effects::kEffectSettings : instrument::kSettings, target.path);
	if (row == nullptr) {
		return std::nullopt;
	}
	if (where.slot) {
		Reader reader{SlotReader{model, *where.slot, facts}};
		if (!reader.resolved()) {
			return std::nullopt;
		}
		return Resolved{row, reader, setting_context(model, *where.slot, facts), engine::kDelugeInstrument};
	}
	if (effect && where.chain) {
		Reader reader{ChainReader{model, *where.chain, facts}};
		if (!reader.resolved()) {
			return std::nullopt;
		}
		return Resolved{row, reader, setting_context(*where.chain, where.chain_owner), engine::kGlobalEffectable};
	}
	return std::nullopt;
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
	const std::string_view partner = engine::mapping_of(*resolved.row).partner_path();
	return partner.empty() ? std::optional<int32_t>{0} : resolved.reader.read(partner);
}

} // namespace

std::optional<project::SettingAddress> setting_address(const Target& target, const Addresses& where) {
	using project::SettingGroupId;
	if (target.kind == Target::Kind::SOUND) {
		if (where.slot || target.audio_clip_path.empty() || !where.clip
		    || where.instrument != engine::SettingContext::Instrument::AUDIO) {
			return std::nullopt;
		}
		return project::SettingAddress{SettingGroupId::AUDIO_CLIP, *where.clip};
	}
	if (target.kind != Target::Kind::MODEL) {
		return std::nullopt;
	}
	const auto on = [&](SettingGroupId group, const auto& owner) -> std::optional<project::SettingAddress> {
		if (!owner) {
			return std::nullopt;
		}
		return project::SettingAddress{group, *owner};
	};
	switch (target.group) {
	case SettingGroupId::SONG:
		return project::SettingAddress{SettingGroupId::SONG, project::SongAddress{}};
	case SettingGroupId::TRACK:
	case SettingGroupId::INSTRUMENT_DEVICE:
	case SettingGroupId::AUDIO_INPUT:
		return on(target.group, where.track);
	case SettingGroupId::CLIP:
	case SettingGroupId::CLIP_VALUES:
	case SettingGroupId::AUDIO_CLIP:
		return on(target.group, where.clip);
	case SettingGroupId::CLIP_ARP:
		return where.lane ? on(SettingGroupId::LANE_ARP, where.lane) : on(SettingGroupId::CLIP_ARP, where.clip);
	case SettingGroupId::LANE:
	case SettingGroupId::LANE_ARP:
		return on(target.group, where.lane);
	case SettingGroupId::NOTE_ROW:
		return on(target.group, where.note_row);
	}
	return std::nullopt;
}

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
	if (!resolved) {
		return 0;
	}
	// The names as the menu shows the row in its context: with no input plugged in, one input entry named "Input".
	const l10n::SettingNames* names = l10n::setting_names(resolved->names_owner, target.path, resolved->context);
	if (names == nullptr) {
		return 0;
	}
	if (resolved->row->kind != engine::SettingKind::CHOICE) {
		// A BOOL or INTEGER row a menu lists: the table names each menu value of its range, in order.
		const auto [least, greatest] = engine::menu_range(*resolved->row);
		size_t written = 0;
		for (int32_t menu = least; menu <= greatest && written < out.size(); ++menu) {
			const auto index = static_cast<size_t>(menu - least);
			if (index >= names->choices.size()) {
				break;
			}
			out[written++] = {.name = names->choices[index]};
		}
		return written;
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
