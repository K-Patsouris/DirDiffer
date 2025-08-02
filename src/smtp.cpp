#include "smtp.h"
#include "int_defs.h"
#include "logger.h"
#include <chrono>
#include <format> // std::format() chrono time_point to string.

#include "curl/curl.h"


namespace diff {

	
	
	struct return_code_pair {
	public:
		enum my_code : u32 {
			all_ok,
			empty_stuff,
			no_curl_init,
			slist_no_alloc,
			email_raw_no_alloc
		};
		
		constexpr return_code_pair() noexcept = default;
		constexpr return_code_pair(const my_code init) noexcept : mine{ init }, curl{ CURLE_OK } {}
		constexpr return_code_pair(const CURLcode init) noexcept : mine{ all_ok }, curl{ init } {}
		
		my_code mine{ all_ok };
		CURLcode curl{ CURLE_OK };
	};
	
	return_code_pair send_email_helper(const smtp_info& smtp, const email_metadata& metadata, u8string_view text) noexcept {
		if (smtp.url.empty()
			or smtp.username.empty()
			or smtp.password.empty()
			or metadata.from.empty()
			or metadata.to.empty()
			or text.empty())
		{
			return return_code_pair::empty_stuff;
		}
		
		// Init curl.
		struct curl_wrapper {
		public:
			~curl_wrapper() noexcept {
				curl_slist_free_all(slist); // nullptr checks unnecessary. Both functions handle them properly.
				curl_easy_cleanup(curl);
			}
			
			[[nodiscard]] bool init() noexcept {
				curl = curl_easy_init();
				if (not curl) {
					result_codes.mine = return_code_pair::no_curl_init;
					return false;
				}
				return true;
			}
			
			[[nodiscard]] bool set_smtp(const smtp_info& smtp) noexcept {
				result_codes.curl = curl_easy_setopt(curl, CURLOPT_URL, smtp.url.c_str());
				
				if (result_codes.curl == CURLE_OK) {
					result_codes.curl = curl_easy_setopt(curl, CURLOPT_USERNAME, smtp.username.c_str());
				}
				
				if (result_codes.curl == CURLE_OK) {
					result_codes.curl = curl_easy_setopt(curl, CURLOPT_PASSWORD, smtp.password.c_str()); 
				}
				
				return result_codes.curl == CURLE_OK;
			}
			
			[[nodiscard]] bool set_from_to(const email_metadata& metadata) noexcept {
				result_codes.curl = curl_easy_setopt(curl, CURLOPT_MAIL_FROM, reinterpret_cast<const char*>(metadata.from.c_str()));
				
				if (result_codes.curl == CURLE_OK) {
					slist = curl_slist_append(slist, reinterpret_cast<const char*>(metadata.to.c_str()));
					if (not slist) {
						result_codes.mine = return_code_pair::slist_no_alloc;
						return false;
					}
					for (const auto& cc : metadata.cc) {
						slist = curl_slist_append(slist, reinterpret_cast<const char*>(cc.c_str()));
						if (not slist) {
							result_codes.mine = return_code_pair::slist_no_alloc;
							return false;
						}
					}
					result_codes.curl = curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, slist);
				}
				
				return result_codes.curl == CURLE_OK;
			}
			
			[[nodiscard]] bool set_feedfunc(std::size_t(*feedfunc)(char*, std::size_t, std::size_t, void*)) noexcept {
				result_codes.curl = curl_easy_setopt(curl, CURLOPT_READFUNCTION, feedfunc);
				
				if (result_codes.curl == CURLE_OK) {
					result_codes.curl = curl_easy_setopt(curl, CURLOPT_READDATA, nullptr);
				}
				
				if (result_codes.curl == CURLE_OK) {
					result_codes.curl = curl_easy_setopt(curl, CURLOPT_UPLOAD, 1l);
				}
				
				return result_codes.curl == CURLE_OK;
			}
			
			[[nodiscard]] bool perform() noexcept {
				result_codes.curl = curl_easy_perform(curl);
				return result_codes.curl == CURLE_OK;
			}
			
			CURL* curl{ nullptr };
			curl_slist* slist{ nullptr };
			return_code_pair result_codes{};
		};
		
		curl_wrapper curl{};
		
		if (not curl.init()) {
			return curl.result_codes;
		}
		if (not curl.set_smtp(smtp)) {
			return curl.result_codes;
		}
		if (not curl.set_from_to(metadata)) {
			return curl.result_codes;
		}
		
		// Generate full email text, including RFC 5322 headers.
		// Static string that we reset to new email contents each time, because I want the feeding function as a lambda, and it needs to be captureless to autoconvert to function pointer.
		static diff::u8string email_raw{};
		
		email_raw = [&]() -> diff::u8string {
			std::size_t total_ascii_length = 100u; // Some space for header names, newlines, etc.
			
			total_ascii_length += metadata.from.length();
			total_ascii_length += metadata.to.length();
			total_ascii_length += metadata.subject.length();
			
			if (not metadata.cc.empty()) {
				total_ascii_length += metadata.cc.size() * 2u; // 2 for each cc, for the ", "
				for (const auto& cc : metadata.cc) {
					total_ascii_length += cc.length();
				}
			}
			
			diff::u8string date_string{};
			
			diff::u8string full_email{};
			try {
				using namespace std::chrono;
				std::string ascii_date = std::format("{:%a, %d %b %Y %H:%M:%S} +0000"sv, std::chrono::floor<seconds>(system_clock::now())); // floor() to discard seconds fractions.
				date_string.resize(ascii_date.length());
				std::memcpy(date_string.data(), ascii_date.c_str(), date_string.length());
				full_email.reserve(total_ascii_length + date_string.length() + text.length() + 200);
			}
			catch (std::exception& ex) {
				log::error("Send Email: Exception thrown: {}"sv, ex.what());
				return {};
			}
			
			full_email.append(u8"Date: ").append(date_string).append(u8"\r\n")
					 .append(u8"From: ").append(metadata.from).append(u8"\r\n")
					 .append(u8"To: ").append(metadata.to).append(u8"\r\n");
			
			if (not metadata.cc.empty()) {
				full_email.append(u8"Cc: ");
				for (const auto& cc : metadata.cc) {
					full_email.append(cc).append(u8", ");
				}
				full_email[full_email.length() - 2] = '\r'; // Transform the trailing ", ", guaranteed to have been there, into "\r\n".
				full_email[full_email.length() - 1] = '\n';
			}
					 
			full_email.append(u8"Subject: ").append(metadata.subject).append(u8"\r\n");
			
			full_email.append(u8"\r\n").append(text); // Extra newline here to signify header end.
			
			return full_email;
		}();
		
		if (email_raw.empty()) {
			return return_code_pair::email_raw_no_alloc;
		}
		
		auto email_text_feeder = [](char* buffer, std::size_t size, std::size_t nmemb, void*) -> std::size_t {
			static std::size_t fed_bytes = 0;
			
			const std::size_t buffer_size = size * nmemb;
			
			const std::size_t remaining_bytes = email_raw.length() - fed_bytes;
			
			const std::size_t feeding_count = buffer_size < remaining_bytes ? buffer_size : remaining_bytes; // Pick least
			
			if (feeding_count == 0) {
				fed_bytes = 0;
				return 0;
			}
			
			std::memcpy(buffer, reinterpret_cast<const char*>(email_raw.c_str()) + fed_bytes, feeding_count);
			
			fed_bytes += feeding_count;
			
			return feeding_count;
		};
		
		if (not curl.set_feedfunc(email_text_feeder)) {
			return curl.result_codes;
		}
		
		// Finally, send the email.
		if (not curl.perform()) {
			return curl.result_codes;
		}
		
		return {};
	}
	
	bool send_email(const smtp_info& smtp, const email_metadata& metadata, u8string_view text) noexcept {
		const return_code_pair result_codes = send_email_helper(smtp, metadata, text);
		
		if (result_codes.mine != return_code_pair::all_ok) {
			switch (result_codes.mine) {
			case return_code_pair::empty_stuff: {
				log::error("Send Email: SMTP url{}, username{}, and password{}, and Email sender{}, recipient{}, and body{} must not be empty."sv,
					smtp.url.empty()      ? " (was empty)" : "",
					smtp.username.empty() ? " (was empty)" : "",
					smtp.password.empty() ? " (was empty)" : "",
					metadata.from.empty() ? " (was empty)" : "",
					metadata.to.empty()   ? " (was empty)" : "",
					text.empty()          ? " (was empty)" : "");
				return false;
			}
			case return_code_pair::no_curl_init: {
				log::error("Send Email: Failed to initialize curl."sv);
				return false;
			}
			case return_code_pair::slist_no_alloc: {
				log::error("Send Email: Failed to allocate Cc curl slist."sv);
				return false;
			}
			case return_code_pair::email_raw_no_alloc: {
				log::error("Send Email: Failed to allocate space for raw email string."sv);
				return false;
			}
			default:  {
				log::error("Send Email: Unspecified error."sv); // Should never get here.
				return false;
			}
			}
		}
		
		if (result_codes.curl != CURLE_OK) {
			log::error("Send Email: curl error: {}"sv, curl_easy_strerror(result_codes.curl));
			return false;
		}
		
		return true;
	}
	
	
}