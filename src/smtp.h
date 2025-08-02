#pragma once
#include "string_defs.h"
#include "vector_defs.h"


namespace diff {
	
	struct smtp_info {
		diff::u8string url{};
		diff::u8string username{};
		diff::u8string password{};
	};
	
	struct email_metadata {
		diff::u8string from{};				// Mandatory
		diff::u8string to{};					// Apparently optional, but I want it always, Primary recipient
		diff::vector<diff::u8string> cc{};		// Optional, Secondary recipients
		diff::u8string subject{};				// Optional
	};
	
	[[nodiscard]] bool send_email(const smtp_info& smtp, const email_metadata& metadata, diff::u8string_view text) noexcept;
	
}
