#pragma once
#include "string_defs.h"
#include <filesystem>
#include <format>
#include <array>

namespace diff {

	class log {
	private:
		
		log() = delete; // Must only be used staticly.
		
		enum severity {
			sev_info,
			sev_warning,
			sev_error,
			sev_critical
		};

		class char_buffer {
		private:
			enum : std::size_t { buffer_size = 500 };

		public:

			class saturating_iterator {
			public:
				using container_type = std::array<char, buffer_size>;
				using value_type = container_type::value_type;
				using difference_type = container_type::difference_type;

				constexpr saturating_iterator() noexcept = default; // No default construction.
				constexpr saturating_iterator(const saturating_iterator&) noexcept = default;
				constexpr saturating_iterator(saturating_iterator&&) noexcept = default;
				constexpr saturating_iterator& operator=(const saturating_iterator&) noexcept = default;
				constexpr saturating_iterator& operator=(saturating_iterator&&) noexcept = default;
				constexpr ~saturating_iterator() noexcept = default;

				constexpr saturating_iterator(container_type& buf) noexcept
					: ptr{ buf.begin() }
					, last_valid{ buf.end() - 1 }
				{}

				constexpr saturating_iterator& operator++() noexcept {
					ptr += (ptr != last_valid);
					return *this;
				}
				constexpr saturating_iterator operator++(int) noexcept {
					saturating_iterator old = *this;
					ptr += (ptr != last_valid);
					return old;
				}
				constexpr value_type& operator*() noexcept { return *ptr; }

				constexpr container_type::iterator get() const noexcept { return ptr; }

			private:
				container_type::iterator ptr;
				container_type::iterator last_valid;

			};
			static_assert(std::output_iterator<saturating_iterator, char>);

			constexpr saturating_iterator get_overwriting_iterator() noexcept { return saturating_iterator{ buf }; }

			constexpr auto begin() const noexcept { return buf.begin(); }
			constexpr auto end() const noexcept { return buf.end(); }

		private:
			std::array<char, buffer_size> buf{};

		};
		
		static bool do_log(string_view msg, const severity sev) noexcept;
		
	public:
		
		[[nodiscard]] static bool init(const std::filesystem::path& log_file_path) noexcept;
		
		static bool info(string_view fmt, auto&&... args) noexcept {
			try {
				static constinit char_buffer buf{};
				return do_log({ buf.begin(), std::vformat_to(buf.get_overwriting_iterator(), fmt, std::make_format_args(args...)).get()}, severity::sev_info);
			}
			catch (...) { return false; }
		}
		
		static bool warning(string_view fmt, auto&&... args) noexcept {
			try {
				static constinit char_buffer buf{};
				return do_log({ buf.begin(), std::vformat_to(buf.get_overwriting_iterator(), fmt, std::make_format_args(args...)).get()}, severity::sev_warning);
			}
			catch (...) { return false; }
		}
		
		static bool error(string_view fmt, auto&&... args) noexcept {
			try {
				static constinit char_buffer buf{};
				return do_log({ buf.begin(), std::vformat_to(buf.get_overwriting_iterator(), fmt, std::make_format_args(args...)).get() }, severity::sev_error);
			}
			catch (...) { return false; }
		}
		
		static bool critical(string_view fmt, auto&&... args) noexcept {
			try {
				static constinit char_buffer buf{};
				return do_log({ buf.begin(), std::vformat_to(buf.get_overwriting_iterator(), fmt, std::make_format_args(args...)).get()}, severity::sev_critical);
			}
			catch (...) { return false; }
		}

	private:
		class log_impl;
		static log_impl impl;
	};

}
