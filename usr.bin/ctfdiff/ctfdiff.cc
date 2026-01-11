#include <getopt.h>
#include <libelf.h>

#include "abi.hpp"
#include "ctfdata.hpp"
#include "metadata.hpp"
#include "utility.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

static constexpr char kAbiToolVersion[] = "ctfdiff-abi-1";

static struct option diff_longopts[] = { { "f-ignore-const", no_argument,
		NULL, 'c' },
	{ "help", no_argument, NULL, 'h' }, { NULL, 0, NULL, 0 } };

static struct option abi_longopts[] = { { "base", required_argument, NULL,
		'b' },
	{ "new", required_argument, NULL, 'n' },
	{ "type", required_argument, NULL, 't' },
	{ "types-file", required_argument, NULL, 'T' },
	{ "json", no_argument, NULL, 'j' },
	{ "max-reasons", required_argument, NULL, 'm' },
	{ "help", no_argument, NULL, 'h' }, { NULL, 0, NULL, 0 } };

static void
print_usage()
{
	std::cout << "ctfdiff compare the SUNW_ctf section of two ELF files\n";
	std::cout << "usage:\n";
	std::cout << "  ctfdiff [options] <file1> <file2>\n";
	std::cout << "  ctfdiff abi-check --base <base> --new <new> --type <type> "
			 "[--type <type>...] [--types-file <path>] [--json] "
			 "[--max-reasons <n>]\n";
	std::cout << "options:\n";
	std::cout << "  -f-ignore-const: ignore const decorator\n";
}

static void
print_abi_usage()
{
	std::cout << "usage:\n";
	std::cout << "  ctfdiff abi-check --base <base> --new <new> --type <type> "
			 "[--type <type>...] [--types-file <path>] [--json] "
			 "[--max-reasons <n>]\n";
}

static std::string
trim_copy(const std::string &input)
{
	size_t start = 0;
	while (start < input.size() &&
	    std::isspace(static_cast<unsigned char>(input[start])))
		++start;
	size_t end = input.size();
	while (end > start &&
	    std::isspace(static_cast<unsigned char>(input[end - 1])))
		--end;
	return input.substr(start, end - start);
}

static bool
read_types_file(const std::string &path, std::vector<std::string> &types,
    std::string &error)
{
	std::ifstream file(path);
	if (!file.is_open()) {
		error = "cannot open types file: " + path;
		return false;
	}

	std::string line;
	while (std::getline(file, line)) {
		std::string trimmed = trim_copy(line);
		if (trimmed.empty())
			continue;
		if (trimmed[0] == '#')
			continue;
		types.push_back(trimmed);
	}

	return true;
}

static std::string
json_escape(const std::string &input)
{
	std::string out;
	out.reserve(input.size() + 8);
	for (unsigned char c : input) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			if (c < 0x20) {
				std::ostringstream hex;
				hex << "\\u00" << std::hex << std::uppercase
				    << std::setw(2) << std::setfill('0')
				    << static_cast<int>(c);
				out += hex.str();
			} else {
				out.push_back(static_cast<char>(c));
			}
			break;
		}
	}
	return out;
}

static void
print_human_result(const AbiTypeResult &result)
{
	std::cout << result.type_name << ": "
		  << abi_verdict_string(result.verdict) << '\n';

	if (result.base_size != 0 || result.new_size != 0) {
		std::cout << "  base_size=" << result.base_size
			  << " new_size=" << result.new_size << '\n';
	}

	for (const auto &change : result.breaking_changes) {
		std::string base_member = change.base_member_name.empty() ?
		    "(anon)" : change.base_member_name;
		std::string new_member = change.new_member_name.empty() ?
		    "(none)" : change.new_member_name;
		std::cout << "  - offset=" << change.key.offset << " width="
			  << change.key.width << ' '
			  << abi_slot_unit_string(change.key.unit)
			  << " base_member=" << base_member
			  << " base_type=" << change.base_fingerprint.summary
			  << " new_member=" << new_member
			  << " new_type=" << change.new_fingerprint.summary
			  << " reason=" << change.reason << '\n';
	}

	for (const auto &note : result.notes)
		std::cout << "  note: " << note << '\n';
}

static void
print_json_results(const std::string &base_file, const std::string &new_file,
    const std::vector<AbiTypeResult> &results)
{
	std::cout << '{';
	std::cout << "\"tool_version\":\"" << json_escape(kAbiToolVersion)
		  << "\",";
	std::cout << "\"base_file\":\"" << json_escape(base_file) << "\",";
	std::cout << "\"new_file\":\"" << json_escape(new_file) << "\",";
	std::cout << "\"results\":[";
	for (size_t i = 0; i < results.size(); ++i) {
		const auto &res = results[i];
		if (i > 0)
			std::cout << ',';
		std::cout << '{';
		std::cout << "\"type\":\"" << json_escape(res.type_name)
			  << "\",";
		std::cout << "\"verdict\":\""
			  << abi_verdict_string(res.verdict) << "\",";
		std::cout << "\"base_size\":" << res.base_size << ',';
		std::cout << "\"new_size\":" << res.new_size << ',';
		std::cout << "\"breaking_changes\":[";
		for (size_t j = 0; j < res.breaking_changes.size(); ++j) {
			const auto &chg = res.breaking_changes[j];
			if (j > 0)
				std::cout << ',';
			std::cout << '{';
			std::cout << "\"offset\":" << chg.key.offset << ',';
			std::cout << "\"width\":" << chg.key.width << ',';
			std::cout << "\"unit\":\""
				  << abi_slot_unit_string(chg.key.unit) << "\",";
			std::cout << "\"base_member_name\":\""
				  << json_escape(chg.base_member_name) << "\",";
			std::cout << "\"new_member_name\":\""
				  << json_escape(chg.new_member_name) << "\",";
			std::cout << "\"base_fingerprint\":\""
				  << json_escape(chg.base_fingerprint.summary) << "\",";
			std::cout << "\"new_fingerprint\":\""
				  << json_escape(chg.new_fingerprint.summary) << "\",";
			std::cout << "\"reason\":\""
				  << json_escape(chg.reason) << "\"";
			std::cout << '}';
		}
		std::cout << "],";
		std::cout << "\"notes\":[";
		for (size_t j = 0; j < res.notes.size(); ++j) {
			if (j > 0)
				std::cout << ',';
			std::cout << "\"" << json_escape(res.notes[j]) << "\"";
		}
		std::cout << ']';
		std::cout << '}';
	}
	std::cout << "]}\n";
}

static int
run_diff(int argc, char *argv[])
{
	char *l_filename = nullptr;
	char *r_filename = nullptr;
	int c;

	opterr = 0;
	optind = 1;
	while ((c = getopt_long_only(argc, argv, "c", diff_longopts, NULL)) !=
	    -1) {
		switch (c) {
		case 'c':
			flags |= F_IGNORE_CONST;
			break;
		case 'h':
			print_usage();
			return 0;
		default:
			print_usage();
			return 1;
		}
	}

	if (optind + 2 != argc) {
		print_usage();
		return 1;
	}

	l_filename = argv[optind];
	r_filename = argv[optind + 1];

	CtfMetaData lhs(l_filename);
	if (!lhs.is_available()) {
		std::cout << "Cannot parse file " << l_filename << '\n';
		return 1;
	}

	CtfMetaData rhs(r_filename);
	if (!rhs.is_available()) {
		std::cout << "Cannot parse file " << r_filename << '\n';
		return 1;
	}

	auto l_info = CtfData::create_ctf_info(std::move(lhs));
	if (l_info == nullptr)
		return 1;

	auto r_info = CtfData::create_ctf_info(std::move(rhs));
	if (r_info == nullptr)
		return 1;

	if ((flags & F_IGNORE_CONST) != 0)
		ignore_ids.push_back(&typeid(CtfTypeConst));

	l_info->compare_and_get_diff(*r_info.get());
	return 0;
}

static int
run_abi_check(int argc, char *argv[])
{
	std::string base_file;
	std::string new_file;
	std::vector<std::string> types;
	bool json = false;
	size_t max_reasons = 20;
	int c;

	opterr = 0;
	optind = 1;
	while ((c = getopt_long(argc, argv, "b:n:t:T:m:jh", abi_longopts,
	    NULL)) != -1) {
		switch (c) {
		case 'b':
			base_file = optarg;
			break;
		case 'n':
			new_file = optarg;
			break;
		case 't':
			types.push_back(optarg);
			break;
		case 'T': {
			std::string error;
			if (!read_types_file(optarg, types, error)) {
				std::cout << error << '\n';
				return 2;
			}
			break;
		}
		case 'm': {
			char *end = nullptr;
			unsigned long value = std::strtoul(optarg, &end, 10);
			if (end == optarg || *end != '\0' || value == 0) {
				std::cout << "invalid --max-reasons value\n";
				return 2;
			}
			max_reasons = value;
			break;
		}
		case 'j':
			json = true;
			break;
		case 'h':
			print_abi_usage();
			return 0;
		default:
			print_abi_usage();
			return 2;
		}
	}

	if (base_file.empty() || new_file.empty() || types.empty()) {
		print_abi_usage();
		return 2;
	}

	CtfMetaData base_meta(base_file);
	if (!base_meta.is_available()) {
		std::cout << "Cannot parse BASE file " << base_file << '\n';
		return 2;
	}

	CtfMetaData new_meta(new_file);
	if (!new_meta.is_available()) {
		std::cout << "Cannot parse NEW file " << new_file << '\n';
		return 2;
	}

	auto base_info = CtfData::create_ctf_info(std::move(base_meta));
	if (base_info == nullptr) {
		std::cout << "Cannot parse BASE CTF" << '\n';
		return 2;
	}

	auto new_info = CtfData::create_ctf_info(std::move(new_meta));
	if (new_info == nullptr) {
		std::cout << "Cannot parse NEW CTF" << '\n';
		return 2;
	}

	AbiCheckOptions options;
	options.max_reasons = max_reasons;
	AbiChecker checker(*base_info.get(), *new_info.get(), options);

	bool any_incompatible = false;
	bool any_unknown = false;
	std::vector<AbiTypeResult> results;
	results.reserve(types.size());

	for (const auto &type : types) {
		AbiTypeResult res = checker.check_type(type);
		if (res.verdict == AbiVerdict::Incompatible)
			any_incompatible = true;
		else if (res.verdict == AbiVerdict::Unknown)
			any_unknown = true;
		results.push_back(std::move(res));
	}

	if (json)
		print_json_results(base_file, new_file, results);
	else
		for (const auto &res : results)
			print_human_result(res);

	if (any_incompatible)
		return 1;
	if (any_unknown)
		return 3;
	return 0;
}

int
main(int argc, char *argv[])
{
	(void)elf_version(EV_CURRENT);
	if (argc > 1 && std::string_view(argv[1]) == "abi-check")
		return run_abi_check(argc - 1, argv + 1);
	return run_diff(argc, argv);
}
