#pragma once
#include "string_defs.h"
#include <optional>
#include <filesystem>


namespace diff::winapi {

	std::optional<u8string> get_owner(const std::filesystem::path& full_path);

}
