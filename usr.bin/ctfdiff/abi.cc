#include "abi.hpp"
#include "ctftype.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

struct TypeQuery {
	std::string original;
	std::string name;
	std::string alt_name;
	std::optional<int> kind;
};

struct LayoutInfo {
	uint64_t size_bytes = 0;
	std::vector<AbiSlot> slots;
	std::string unknown_reason;
	bool unknown = false;
};

struct FingerprintContext {
	const CtfData &data;
	size_t max_depth;
	std::unordered_map<uint64_t, AbiFingerprint> cache;
	std::unordered_set<uint32_t> stack;
};

struct SlotKeyHash {
	size_t operator()(const AbiSlotKey &key) const
	{
		uint64_t h = static_cast<uint64_t>(key.offset);
		h ^= static_cast<uint64_t>(key.width) << 1;
		h ^= static_cast<uint64_t>(key.unit == AbiSlotKey::Unit::Bit);
		return std::hash<uint64_t>()(h);
	}
};

struct SlotKeyEq {
	bool operator()(const AbiSlotKey &lhs, const AbiSlotKey &rhs) const
	{
		return lhs.unit == rhs.unit && lhs.offset == rhs.offset &&
		    lhs.width == rhs.width;
	}
};

struct OffsetKey {
	AbiSlotKey::Unit unit;
	uint64_t offset;

	bool operator==(const OffsetKey &rhs) const
	{
		return unit == rhs.unit && offset == rhs.offset;
	}
};

struct OffsetKeyHash {
	size_t operator()(const OffsetKey &key) const
	{
		uint64_t h = static_cast<uint64_t>(key.offset);
		h ^= static_cast<uint64_t>(key.unit == AbiSlotKey::Unit::Bit);
		return std::hash<uint64_t>()(h);
	}
};

struct HashBuilder {
	uint64_t value = 1469598103934665603ULL;

	void add_byte(uint8_t byte)
	{
		value ^= byte;
		value *= 1099511628211ULL;
	}

	void add_u64(uint64_t v)
	{
		for (int i = 0; i < 8; ++i) {
			add_byte(static_cast<uint8_t>((v >> (i * 8)) & 0xff));
		}
	}
};

static std::string
trim_copy(std::string_view input)
{
	size_t start = 0;
	while (start < input.size() &&
	    std::isspace(static_cast<unsigned char>(input[start])))
		++start;
	size_t end = input.size();
	while (end > start &&
	    std::isspace(static_cast<unsigned char>(input[end - 1])))
		--end;
	return std::string(input.substr(start, end - start));
}

static bool
starts_with_kw(const std::string &value, const char *kw, size_t kwlen)
{
	if (value.size() <= kwlen)
		return false;
	if (value.compare(0, kwlen, kw) != 0)
		return false;
	return std::isspace(static_cast<unsigned char>(value[kwlen]));
}

static TypeQuery
parse_type_query(std::string_view raw)
{
	TypeQuery query;
	query.original = trim_copy(raw);
	query.name = query.original;

	if (starts_with_kw(query.name, "struct", 6)) {
		query.kind = CTF_K_STRUCT;
		query.name = trim_copy(query.name.substr(6));
		query.alt_name = query.original;
	} else if (starts_with_kw(query.name, "union", 5)) {
		query.kind = CTF_K_UNION;
		query.name = trim_copy(query.name.substr(5));
		query.alt_name = query.original;
	} else if (starts_with_kw(query.name, "enum", 4)) {
		query.kind = CTF_K_ENUM;
		query.name = trim_copy(query.name.substr(4));
		query.alt_name = query.original;
	}

	return query;
}

static const CtfType *
lookup_type(const CtfData &data, uint32_t id)
{
	const auto &map = data.id_mapper();
	auto it = map.find(id);
	if (it == map.end())
		return nullptr;
	return it->second.get();
}

static const CtfType *
resolve_aliases(const CtfType &type)
{
	const CtfType *current = &type;
	std::unordered_set<uint32_t> seen;

	while (true) {
		if (dynamic_cast<const CtfTypePtr *>(current) != nullptr)
			return current;
		const CtfTypeTypeDef *td =
		    dynamic_cast<const CtfTypeTypeDef *>(current);
		const CtfTypeConst *tc = dynamic_cast<const CtfTypeConst *>(current);
		const CtfTypeVolatile *tv =
		    dynamic_cast<const CtfTypeVolatile *>(current);
		const CtfTypeRestrict *tr =
		    dynamic_cast<const CtfTypeRestrict *>(current);
		const CtfTypeQualifier *qual = nullptr;

		if (td != nullptr)
			qual = td;
		else if (tc != nullptr)
			qual = tc;
		else if (tv != nullptr)
			qual = tv;
		else if (tr != nullptr)
			qual = tr;
		else
			break;

		if (current->get_owned() == nullptr)
			return nullptr;
		uint32_t next_id = qual->ref();
		if (!seen.insert(next_id).second)
			return nullptr;
		const CtfType *next = lookup_type(*current->get_owned(), next_id);
		if (next == nullptr)
			return nullptr;
		current = next;
	}

	return current;
}

static const CtfType *
find_type_by_name(const CtfData &data, const TypeQuery &query,
    std::string &note)
{
	const CtfType *match = nullptr;
	bool ambiguous = false;

	for (const auto &entry : data.id_mapper()) {
		const auto &type = entry.second;
		if (type->name().empty())
			continue;
		bool name_match = type->name() == query.name ||
		    (!query.alt_name.empty() && type->name() == query.alt_name);
		if (!name_match)
			continue;
		if (query.kind.has_value() && type->kind() != query.kind.value())
			continue;

		if (match == nullptr) {
			match = type.get();
			continue;
		}
		if (!query.kind.has_value() && match->kind() != type->kind())
			ambiguous = true;
	}

	if (ambiguous) {
		note = "ambiguous type name; specify struct/union/enum";
		return nullptr;
	}
	if (match == nullptr) {
		note = "type not found";
		return nullptr;
	}

	return match;
}

static uint64_t
cache_key(uint32_t id, size_t depth)
{
	return (static_cast<uint64_t>(depth) << 32) | id;
}

static AbiFingerprint
unknown_fingerprint(const std::string &summary)
{
	return { AbiFingerprint::Kind::Unknown, 0, summary, true };
}

static bool
fingerprint_equal(const AbiFingerprint &lhs, const AbiFingerprint &rhs)
{
	if (lhs.unknown || rhs.unknown)
		return false;
	return lhs.kind == rhs.kind && lhs.hash == rhs.hash;
}

static bool
type_size_bytes(const CtfType &type, uint64_t &size, std::string &reason);

static AbiFingerprint
fingerprint_type(const CtfType &type, size_t depth, FingerprintContext &ctx);

static bool
member_is_bitfield(const CtfType &type, uint64_t member_offset)
{
	const CtfTypeInteger *int_type =
	    dynamic_cast<const CtfTypeInteger *>(&type);
	if (int_type == nullptr)
		return false;

	if (member_offset % 8 != 0)
		return true;

	if (int_type->offset() != 0)
		return true;

	if (int_type->width() % 8 != 0)
		return true;

	uint64_t size_bytes = type.type_size();
	if (size_bytes == 0)
		return false;
	return int_type->width() != size_bytes * 8;
}

static bool
collect_slots(const CtfType &type, size_t depth, FingerprintContext &ctx,
    LayoutInfo &layout)
{
	const CtfTypeComplex *complex =
	    dynamic_cast<const CtfTypeComplex *>(&type);
	if (complex == nullptr) {
		layout.unknown = true;
		layout.unknown_reason = "type is not struct/union";
		return false;
	}

	layout.size_bytes = complex->byte_size();

	for (const auto &member : complex->members()) {
		const CtfType *member_type = lookup_type(ctx.data, member.type_id);
		if (member_type == nullptr) {
			layout.unknown = true;
			layout.unknown_reason = "member type missing";
			return false;
		}

		const CtfType *resolved = resolve_aliases(*member_type);
		if (resolved == nullptr) {
			layout.unknown = true;
			layout.unknown_reason = "failed to resolve member type";
			return false;
		}

		bool is_bitfield = member_is_bitfield(*resolved, member.offset);
		AbiSlot slot{};
		slot.member_name = std::string(member.name);

		if (!is_bitfield && (member.offset % 8 != 0)) {
			layout.unknown = true;
			layout.unknown_reason =
			    "non-bitfield member at bit offset";
			return false;
		}

		AbiFingerprint fp = fingerprint_type(*resolved, depth, ctx);
		if (fp.unknown) {
			layout.unknown = true;
			layout.unknown_reason = fp.summary;
			return false;
		}
		slot.fingerprint = std::move(fp);

		if (is_bitfield) {
			const CtfTypeInteger *int_type =
			    dynamic_cast<const CtfTypeInteger *>(resolved);
			if (int_type == nullptr) {
				layout.unknown = true;
				layout.unknown_reason = "bitfield type is not integer";
				return false;
			}
			slot.key = { AbiSlotKey::Unit::Bit, member.offset,
				static_cast<uint64_t>(int_type->width()) };
		} else {
			uint64_t member_size = 0;
			std::string reason;
			if (!type_size_bytes(*resolved, member_size, reason)) {
				layout.unknown = true;
				layout.unknown_reason = reason;
				return false;
			}
			slot.key = { AbiSlotKey::Unit::Byte, member.offset / 8,
				member_size };
		}

		layout.slots.push_back(std::move(slot));
	}

	return true;
}

static AbiFingerprint
fingerprint_type(const CtfType &type, size_t depth, FingerprintContext &ctx)
{
	const CtfType *resolved = resolve_aliases(type);
	if (resolved == nullptr)
		return unknown_fingerprint("unresolvable type");

	uint32_t id = resolved->type_id();
	uint64_t key = cache_key(id, depth);
	auto it = ctx.cache.find(key);
	if (it != ctx.cache.end())
		return it->second;

	AbiFingerprint fp = unknown_fingerprint("unsupported type");

	if (dynamic_cast<const CtfTypeInteger *>(resolved) != nullptr) {
		const auto *int_type =
		    dynamic_cast<const CtfTypeInteger *>(resolved);
		HashBuilder hb;
		hb.add_u64(static_cast<uint64_t>(AbiFingerprint::Kind::Integer));
		hb.add_u64(int_type->encoding());
		hb.add_u64(int_type->width());
		std::ostringstream summary;
		summary << "int:enc=0x" << std::hex << int_type->encoding()
			<< std::dec << " bits=" << int_type->width();
		fp = { AbiFingerprint::Kind::Integer, hb.value, summary.str(),
			false };
	} else if (dynamic_cast<const CtfTypeFloat *>(resolved) != nullptr) {
		const auto *flt_type =
		    dynamic_cast<const CtfTypeFloat *>(resolved);
		HashBuilder hb;
		hb.add_u64(static_cast<uint64_t>(AbiFingerprint::Kind::Float));
		hb.add_u64(flt_type->encoding());
		hb.add_u64(flt_type->width());
		std::ostringstream summary;
		summary << "float:enc=0x" << std::hex << flt_type->encoding()
			<< std::dec << " bits=" << flt_type->width();
		fp = { AbiFingerprint::Kind::Float, hb.value, summary.str(), false };
	} else if (dynamic_cast<const CtfTypeEnum *>(resolved) != nullptr) {
		uint64_t size = resolved->type_size();
		if (size == 0) {
			fp = unknown_fingerprint("enum size unavailable");
		} else {
			HashBuilder hb;
			hb.add_u64(static_cast<uint64_t>(AbiFingerprint::Kind::Enum));
			hb.add_u64(size);
			std::ostringstream summary;
			summary << "enum:size=" << size;
			fp = { AbiFingerprint::Kind::Enum, hb.value, summary.str(),
				false };
		}
	} else if (dynamic_cast<const CtfTypePtr *>(resolved) != nullptr) {
		uint64_t ptr_size = resolved->get_owned()->pointer_size();
		if (ptr_size == 0) {
			fp = unknown_fingerprint("pointer size unavailable");
		} else {
			HashBuilder hb;
			hb.add_u64(static_cast<uint64_t>(AbiFingerprint::Kind::Pointer));
			hb.add_u64(ptr_size);
			std::ostringstream summary;
			summary << "ptr:size=" << ptr_size;
			fp = { AbiFingerprint::Kind::Pointer, hb.value, summary.str(),
				false };
		}
	} else if (dynamic_cast<const CtfTypeArray *>(resolved) != nullptr) {
		const auto *arr = dynamic_cast<const CtfTypeArray *>(resolved);
		const CtfType *elem = lookup_type(ctx.data, arr->contents());
		if (elem == nullptr) {
			fp = unknown_fingerprint("array element type missing");
		} else {
			AbiFingerprint elem_fp = fingerprint_type(*elem, depth, ctx);
			if (elem_fp.unknown) {
				fp = unknown_fingerprint("array element fingerprint unknown");
			} else {
				HashBuilder hb;
				hb.add_u64(
				    static_cast<uint64_t>(AbiFingerprint::Kind::Array));
				hb.add_u64(arr->members());
				hb.add_u64(static_cast<uint64_t>(elem_fp.kind));
				hb.add_u64(elem_fp.hash);
				std::ostringstream summary;
				summary << "array:count=" << arr->members()
					<< " elem=" << elem_fp.summary;
				fp = { AbiFingerprint::Kind::Array, hb.value,
					summary.str(), false };
			}
		}
	} else if (resolved->kind() == CTF_K_STRUCT ||
	    resolved->kind() == CTF_K_UNION) {
		if (depth == 0) {
			/* Fallback to size-only fingerprint when depth is exhausted. */
			uint64_t size = resolved->type_size();
			if (size == 0) {
				fp = unknown_fingerprint("struct/union depth limit");
			} else {
				HashBuilder hb;
				AbiFingerprint::Kind k =
				    resolved->kind() == CTF_K_STRUCT ?
				    AbiFingerprint::Kind::Struct :
				    AbiFingerprint::Kind::Union;
				hb.add_u64(static_cast<uint64_t>(k));
				hb.add_u64(size);
				std::ostringstream summary;
				summary << (resolved->kind() == CTF_K_STRUCT ?
				    "struct" : "union")
					<< ":size=" << size
					<< " (depth-limit)";
				fp = { k, hb.value, summary.str(), false };
			}
		} else if (!ctx.stack.insert(id).second) {
			fp = unknown_fingerprint("recursive struct/union");
		} else {
			LayoutInfo layout;
			if (!collect_slots(*resolved, depth - 1, ctx, layout)) {
				fp = unknown_fingerprint(layout.unknown_reason);
			} else {
				struct SlotSig {
					AbiSlotKey key;
					AbiFingerprint::Kind kind;
					uint64_t hash;
				};
				std::vector<SlotSig> sigs;
				sigs.reserve(layout.slots.size());
				for (const auto &slot : layout.slots) {
					sigs.push_back({ slot.key, slot.fingerprint.kind,
						slot.fingerprint.hash });
				}
				std::sort(sigs.begin(), sigs.end(),
				    [](const SlotSig &a, const SlotSig &b) {
					if (a.key.unit != b.key.unit)
						return a.key.unit < b.key.unit;
					if (a.key.offset != b.key.offset)
						return a.key.offset < b.key.offset;
					if (a.key.width != b.key.width)
						return a.key.width < b.key.width;
					if (a.kind != b.kind)
						return a.kind < b.kind;
					return a.hash < b.hash;
				    });

				HashBuilder hb;
				AbiFingerprint::Kind k = resolved->kind() == CTF_K_STRUCT ?
				    AbiFingerprint::Kind::Struct :
				    AbiFingerprint::Kind::Union;
				hb.add_u64(static_cast<uint64_t>(k));
				for (const auto &sig : sigs) {
					hb.add_u64(static_cast<uint64_t>(sig.key.unit));
					hb.add_u64(sig.key.offset);
					hb.add_u64(sig.key.width);
					hb.add_u64(static_cast<uint64_t>(sig.kind));
					hb.add_u64(sig.hash);
				}
				std::ostringstream summary;
				summary << (resolved->kind() == CTF_K_STRUCT ? "struct" :
				    "union")
					<< ":layout=0x" << std::hex << hb.value;
				fp = { k, hb.value, summary.str(), false };
			}
			ctx.stack.erase(id);
		}
	}

	ctx.cache.emplace(key, fp);
	return fp;
}

static bool
type_size_bytes(const CtfType &type, uint64_t &size, std::string &reason)
{
	const CtfType *resolved = resolve_aliases(type);
	if (resolved == nullptr) {
		reason = "unresolvable type";
		return false;
	}

	int kind = resolved->kind();
	if (kind == CTF_K_POINTER) {
		size_t ptr_size = resolved->get_owned()->pointer_size();
		if (ptr_size == 0) {
			reason = "pointer size unavailable";
			return false;
		}
		size = ptr_size;
		return true;
	}

	if (kind == CTF_K_ARRAY) {
		const CtfTypeArray *arr =
		    dynamic_cast<const CtfTypeArray *>(resolved);
		if (arr == nullptr) {
			reason = "array type mismatch";
			return false;
		}
		const CtfType *elem = lookup_type(*resolved->get_owned(),
		    arr->contents());
		if (elem == nullptr) {
			reason = "array element type missing";
			return false;
		}
		uint64_t elem_size = 0;
		std::string elem_reason;
		if (!type_size_bytes(*elem, elem_size, elem_reason)) {
			reason = elem_reason;
			return false;
		}
		size = elem_size * arr->members();
		return true;
	}

	if (kind == CTF_K_FUNCTION) {
		reason = "function type has no size";
		return false;
	}

	if (kind == CTF_K_FORWARD || kind == CTF_K_UNKNOWN) {
		reason = "incomplete type";
		return false;
	}

	size = resolved->type_size();
	if (size == 0) {
		reason = "type size unavailable";
		return false;
	}

	return true;
}

static bool
is_struct_or_union(const CtfType &type)
{
	return type.kind() == CTF_K_STRUCT || type.kind() == CTF_K_UNION;
}

static std::vector<AbiBreakingChange>
compare_slots(const LayoutInfo &base_layout, const LayoutInfo &new_layout,
    size_t max_reasons)
{
	std::unordered_map<AbiSlotKey, std::vector<AbiSlot>, SlotKeyHash,
	    SlotKeyEq>
	    new_by_key;
	std::unordered_map<OffsetKey, std::vector<AbiSlot>, OffsetKeyHash>
	    new_by_offset;

	for (const auto &slot : new_layout.slots) {
		new_by_key[slot.key].push_back(slot);
		OffsetKey ok{ slot.key.unit, slot.key.offset };
		new_by_offset[ok].push_back(slot);
	}

	std::vector<AbiBreakingChange> breaks;

	for (const auto &base_slot : base_layout.slots) {
		if (breaks.size() >= max_reasons)
			break;
		auto it = new_by_key.find(base_slot.key);
		if (it != new_by_key.end()) {
			bool matched = false;
			for (const auto &cand : it->second) {
				if (fingerprint_equal(base_slot.fingerprint,
					cand.fingerprint)) {
					matched = true;
					break;
				}
			}
			if (matched)
				continue;

			const AbiSlot &cand = it->second.front();
			std::string reason = "TYPE_CHANGED";
			if ((base_slot.fingerprint.kind ==
				    AbiFingerprint::Kind::Struct ||
				    base_slot.fingerprint.kind ==
					AbiFingerprint::Kind::Union) &&
			    (cand.fingerprint.kind == AbiFingerprint::Kind::Struct ||
				cand.fingerprint.kind == AbiFingerprint::Kind::Union))
				reason = "EMBEDDED_LAYOUT_CHANGED";

			breaks.push_back({ base_slot.key, base_slot.member_name,
			    cand.member_name, base_slot.fingerprint, cand.fingerprint,
			    reason });
			continue;
		}

		OffsetKey ok{ base_slot.key.unit, base_slot.key.offset };
		auto off_it = new_by_offset.find(ok);
		if (off_it != new_by_offset.end()) {
			const AbiSlot &cand = off_it->second.front();
			breaks.push_back({ base_slot.key, base_slot.member_name,
			    cand.member_name, base_slot.fingerprint, cand.fingerprint,
			    "WIDTH_CHANGED" });
			continue;
		}

		breaks.push_back({ base_slot.key, base_slot.member_name, "",
		    base_slot.fingerprint, unknown_fingerprint("missing"),
		    "MISSING_SLOT" });
	}

	return breaks;
}

static AbiTypeResult
compare_struct_layouts(const std::string &type_name, const CtfType &base_type,
    const CtfType &new_type, const AbiCheckOptions &options)
{
	AbiTypeResult result;
	result.type_name = type_name;
	result.verdict = AbiVerdict::Unknown;

	FingerprintContext base_ctx{ *base_type.get_owned(), options.max_depth,
		{}, {} };
	FingerprintContext new_ctx{ *new_type.get_owned(), options.max_depth,
		{}, {} };

	LayoutInfo base_layout;
	LayoutInfo new_layout;

	if (!collect_slots(base_type, options.max_depth, base_ctx, base_layout)) {
		result.verdict = AbiVerdict::Unknown;
		result.notes.push_back(base_layout.unknown_reason);
		return result;
	}
	result.base_size = base_layout.size_bytes;
	if (!collect_slots(new_type, options.max_depth, new_ctx, new_layout)) {
		result.verdict = AbiVerdict::Unknown;
		result.notes.push_back(new_layout.unknown_reason);
		return result;
	}

	result.new_size = new_layout.size_bytes;

	result.breaking_changes =
	    compare_slots(base_layout, new_layout, options.max_reasons);

	if (!result.breaking_changes.empty()) {
		result.verdict = AbiVerdict::Incompatible;
		return result;
	}

	result.verdict = AbiVerdict::Compatible;
	if (result.base_size != 0 && result.new_size != 0 &&
	    result.base_size != result.new_size) {
		std::ostringstream note;
		note << "size changed but compatible (base=" << result.base_size
		     << ", new=" << result.new_size
		     << "); extension OK: all BASE slots preserved";
		result.notes.push_back(note.str());
	}

	return result;
}

static AbiTypeResult
compare_simple_types(const std::string &type_name, const CtfType &base_type,
    const CtfType &new_type, const AbiCheckOptions &options)
{
	AbiTypeResult result;
	result.type_name = type_name;
	result.verdict = AbiVerdict::Unknown;

	FingerprintContext base_ctx{ *base_type.get_owned(), options.max_depth,
		{}, {} };
	FingerprintContext new_ctx{ *new_type.get_owned(), options.max_depth,
		{}, {} };

	AbiFingerprint base_fp =
	    fingerprint_type(base_type, options.max_depth, base_ctx);
	AbiFingerprint new_fp =
	    fingerprint_type(new_type, options.max_depth, new_ctx);

	if (base_fp.unknown || new_fp.unknown) {
		result.verdict = AbiVerdict::Unknown;
		if (base_fp.unknown)
			result.notes.push_back(base_fp.summary);
		if (new_fp.unknown)
			result.notes.push_back(new_fp.summary);
		return result;
	}

	uint64_t base_size = 0;
	uint64_t new_size = 0;
	std::string reason;
	if (type_size_bytes(base_type, base_size, reason))
		result.base_size = base_size;
	if (type_size_bytes(new_type, new_size, reason))
		result.new_size = new_size;

	if (fingerprint_equal(base_fp, new_fp)) {
		result.verdict = AbiVerdict::Compatible;
		return result;
	}

	AbiSlotKey key{ AbiSlotKey::Unit::Byte, 0, base_size };
	result.breaking_changes.push_back(
	    { key, "", "", base_fp, new_fp, "TYPE_CHANGED" });
	result.verdict = AbiVerdict::Incompatible;
	return result;
}

} // namespace

AbiChecker::AbiChecker(const CtfData &base, const CtfData &newer,
    const AbiCheckOptions &options)
    : base_(base)
    , new_(newer)
    , options_(options)
{
}

AbiTypeResult
AbiChecker::check_type(std::string_view type_name) const
{
	return abi_check_type(base_, new_, type_name, options_);
}

AbiTypeResult
abi_check_type(const CtfData &base, const CtfData &newer,
    std::string_view type_name, const AbiCheckOptions &options)
{
	TypeQuery query = parse_type_query(type_name);
	AbiTypeResult result;
	result.type_name = query.original;
	result.verdict = AbiVerdict::Unknown;

	std::string base_note;
	const CtfType *base_type = find_type_by_name(base, query, base_note);
	if (base_type == nullptr) {
		result.notes.push_back("BASE: " + base_note);
		return result;
	}

	std::string new_note;
	const CtfType *new_type = find_type_by_name(newer, query, new_note);
	if (new_type == nullptr) {
		result.notes.push_back("NEW: " + new_note);
		return result;
	}

	const CtfType *base_resolved = resolve_aliases(*base_type);
	const CtfType *new_resolved = resolve_aliases(*new_type);
	if (base_resolved == nullptr || new_resolved == nullptr) {
		result.notes.push_back("failed to resolve typedef/qualifiers");
		return result;
	}

	bool base_sou = is_struct_or_union(*base_resolved);
	bool new_sou = is_struct_or_union(*new_resolved);

	if (base_sou && new_sou && base_resolved->kind() == new_resolved->kind())
		return compare_struct_layouts(query.original, *base_resolved,
		    *new_resolved, options);

	if (base_sou && (!new_sou || base_resolved->kind() != new_resolved->kind())) {
		FingerprintContext base_ctx{ *base_resolved->get_owned(),
			options.max_depth, {}, {} };
		LayoutInfo base_layout;
		if (!collect_slots(*base_resolved, options.max_depth, base_ctx,
		    base_layout)) {
			result.verdict = AbiVerdict::Unknown;
			result.notes.push_back(base_layout.unknown_reason);
			return result;
		}

		LayoutInfo empty_layout;
		result.base_size = base_layout.size_bytes;
		result.breaking_changes = compare_slots(base_layout,
		    empty_layout, options.max_reasons);
		result.verdict = AbiVerdict::Incompatible;
		std::ostringstream note;
		note << "NEW type kind differs from BASE";
		result.notes.push_back(note.str());
		return result;
	}

	return compare_simple_types(query.original, *base_resolved, *new_resolved,
	    options);
}

std::string
abi_verdict_string(AbiVerdict verdict)
{
	switch (verdict) {
	case AbiVerdict::Compatible:
		return "COMPATIBLE";
	case AbiVerdict::Incompatible:
		return "INCOMPATIBLE";
	case AbiVerdict::Unknown:
		return "UNKNOWN";
	}
	return "UNKNOWN";
}

std::string
abi_slot_unit_string(AbiSlotKey::Unit unit)
{
	return unit == AbiSlotKey::Unit::Bit ? "bit" : "byte";
}
