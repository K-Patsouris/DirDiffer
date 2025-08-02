#pragma once
#include "string_defs.h"
#include "vector_defs.h"
#include "file.h"
#include <optional>

namespace diff {
	
	struct old_files_t {
		explicit constexpr old_files_t() noexcept = default;
		explicit constexpr old_files_t(const diff::vector<file>& init) : files{ init } {}
		diff::vector<file> files{};
	};
	struct new_files_t {
		explicit constexpr new_files_t() noexcept = default;
		explicit constexpr new_files_t(const diff::vector<file>& init) : files{ init } {}
		diff::vector<file> files{};
	};
	
	std::optional<u8string> diff_sorted_files(const old_files_t& olds, const new_files_t& news) noexcept;
}

