#pragma once

#include <libelf.h>

#include "utility.hpp"
#include <string>
#include <string_view>

struct CtfMetaData {
    private:
	int data_fd;
	std::string filename;
	Elf *elf;
	size_t pointer_size_bytes;

	bool from_elf_file();
	bool from_raw_file();

    public:
	Buffer ctfdata{};
	Buffer symdata{};
	Buffer strdata{};

	CtfMetaData(const std::string &filename);
	~CtfMetaData();

	std::string_view file_name() { return this->filename; }
	size_t pointer_size() const { return pointer_size_bytes; }
	bool is_available();
};
