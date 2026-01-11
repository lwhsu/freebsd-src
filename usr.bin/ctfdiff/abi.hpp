#pragma once

#include "ctfdata.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class AbiVerdict { Compatible, Incompatible, Unknown };

struct AbiSlotKey {
	enum class Unit { Byte, Bit };
	Unit unit;
	uint64_t offset;
	uint64_t width;
};

struct AbiFingerprint {
	enum class Kind {
		Integer,
		Float,
		Enum,
		Pointer,
		Struct,
		Union,
		Array,
		Unknown,
	};

	Kind kind;
	uint64_t hash;
	std::string summary;
	bool unknown;
};

struct AbiSlot {
	AbiSlotKey key;
	std::string member_name;
	AbiFingerprint fingerprint;
};

struct AbiBreakingChange {
	AbiSlotKey key;
	std::string base_member_name;
	std::string new_member_name;
	AbiFingerprint base_fingerprint;
	AbiFingerprint new_fingerprint;
	std::string reason;
};

struct AbiTypeResult {
	std::string type_name;
	AbiVerdict verdict = AbiVerdict::Unknown;
	uint64_t base_size = 0;
	uint64_t new_size = 0;
	std::vector<AbiBreakingChange> breaking_changes;
	std::vector<std::string> notes;
};

struct AbiCheckOptions {
	size_t max_reasons = 20;
	size_t max_depth = 2;
};

class AbiChecker {
    public:
	AbiChecker(const CtfData &base, const CtfData &newer,
	    const AbiCheckOptions &options);
	AbiTypeResult check_type(std::string_view type_name) const;

    private:
	const CtfData &base_;
	const CtfData &new_;
	AbiCheckOptions options_;
};

AbiTypeResult abi_check_type(const CtfData &base, const CtfData &newer,
    std::string_view type_name, const AbiCheckOptions &options);

std::string abi_verdict_string(AbiVerdict verdict);
std::string abi_slot_unit_string(AbiSlotKey::Unit unit);
