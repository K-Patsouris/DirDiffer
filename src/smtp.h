#pragma once
#include "string_defs.h"
#include <vector>


namespace diff {
	
	struct smtp_info {
		u8string url{};
		u8string username{};
		u8string password{};
	};
	
	struct email_metadata {
		u8string from{};				// Mandatory
		u8string to{};					// Apparently optional, but I want it always, Primary recipient
		std::vector<u8string> cc{};		// Optional, Secondary recipients
		u8string subject{};				// Optional
	};
	
	[[nodiscard]] bool send_email(const smtp_info& smtp, const email_metadata& metadata, u8string_view text) noexcept;
	
}
