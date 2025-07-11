#pragma once
#include "dynamic_buffer.h"
#include "file.h"
#include "smtp.h"
#include <vector>
#include <optional>

#include <mutex>


namespace diff {
	
	// Stateless class, used for friendship and code grouping.
	class serialization {
	public:
	
		struct simple_pair {
			smtp_info smtp{};
			std::vector<file> files{};
		};


		[[nodiscard]] static std::optional<simple_pair> deserialize_from_buffer(const dynamic_buffer& buf) noexcept;
		

		[[nodiscard]] static std::optional<dynamic_buffer> serialize_to_buffer_encrypted(const smtp_info& smtp, const std::vector<file>& files) noexcept;
		[[nodiscard]] static std::optional<dynamic_buffer> serialize_to_buffer_unencrypted(const smtp_info& smtp, const std::vector<file>& files) noexcept;
		
	};

}



















