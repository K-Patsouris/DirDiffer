#pragma once
#include "int_defs.h"
#include "string_defs.h"
#include <vector>
#include <optional>
#include <filesystem>
#include "lowercase_path.h"
#include "smtp.h"

#include "winapi_funcs.h"

namespace diff {
	
	class configuration {
	public:
		static std::optional<configuration> parse_file_contents(const u8string& contents) noexcept;
		
		constexpr configuration() noexcept = default;
		constexpr configuration(const configuration&) = default;
		constexpr configuration(configuration&&) noexcept = default;
		constexpr configuration& operator=(const configuration&) = default;
		constexpr configuration& operator=(configuration&&) noexcept = default;
		constexpr ~configuration() noexcept = default;
		
		[[nodiscard]] const std::filesystem::path& get_root() const noexcept { return root; }
		
		[[nodiscard]] u32 get_min_depth() const noexcept { return min_depth; }
		
		[[nodiscard]] const email_metadata& get_email_metadata() const noexcept { return email; }

		[[nodiscard]] bool folder_is_excluded(const lowercase_path& folder_path) const noexcept;
		
		[[nodiscard]] bool ext_is_accepted(const lowercase_path& ext) const noexcept;
		
		
		u8string dump() const;


	private:
		std::filesystem::path root{};
		std::vector<lowercase_path> extensions{};
		std::vector<lowercase_path> excluded_folders{}; // Relative to root.
		u32 min_depth{};
		email_metadata email{};
	};
	
}