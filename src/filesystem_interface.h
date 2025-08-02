#pragma once
#include "logger.h"
#include "string_defs.h"
#include "vector_defs.h"
#include "file.h"
#include "configuration.h"
#include "dynamic_buffer.h"
#include <filesystem>
#include <optional>


namespace diff {
	
	[[nodiscard]] std::optional<diff::u8string> read_from_file(const std::filesystem::path& file_path) noexcept;
	
	[[nodiscard]] bool write_to_file(const std::filesystem::path& file_path, string_view data) noexcept;
	[[nodiscard]] bool write_to_file(const std::filesystem::path& file_path, u8string_view data) noexcept;
	
	
	[[nodiscard]] std::optional<dynamic_buffer> read_dbuf_from_file(const std::filesystem::path& file_path) noexcept;
	
	[[nodiscard]] bool write_dbuf_to_file(const std::filesystem::path& file_path, const dynamic_buffer& dbuf) noexcept;
	

	[[nodiscard]] std::optional<configuration> get_configuration(const std::filesystem::path& file_path) noexcept;

	
	[[nodiscard]] std::optional<diff::vector<file>> get_files_recursive(const configuration& filter) noexcept;
	
	
	[[nodiscard]] std::optional<bool> file_exists(const std::filesystem::path& file_path) noexcept;
	
	[[nodiscard]] bool rename_file(const std::filesystem::path& file_path, string_view new_name) noexcept; // Extension not preserved. Include it in new_name
	
	[[nodiscard]] bool delete_file(const std::filesystem::path& file_path) noexcept;
	
	[[nodiscard]] bool folder_create_or_exists(const std::filesystem::path& folder_name) noexcept;
	
}