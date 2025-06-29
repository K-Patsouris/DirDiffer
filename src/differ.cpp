#include "differ.h"
#include "string_utils.h"
#include "logger.h"


namespace diff {
	
	
	struct diff_string_maker {
	private:
	
		struct name_attrs {
			
			constexpr name_attrs(const u8string& stem) {
				if (stem.empty()) {
					return;
				}
				
				std::vector<u8string> parts{ [](const u8string& stem) {
					/*
					Atypical string split. Keeps the delimiter at the START of each part. Additionally, trims leading & trailing whitespace of each part after split is done.
					Visually, imagine just breaking the string before each delim, without removing anything. Examples with delim '/':
						str "aa/bb/cc/",	result { "aa", "/bb", "/cc", "/" }
						str "aa/bb/cc",		result { "aa", "/bb", "/cc" }
						str "/aa/bb/cc",	result { "/", "aa", "/bb", "/cc" }
						str "aa",			result { "aa" }
					*/
					
					if (stem.empty()) {
						return std::vector<u8string>{};
					}

					std::vector<u8string> ret{};

					size_t start = 0;

					for (std::size_t i = 0; i < stem.length(); ++i) {
						if (stem[i] == u8'#') {
							ret.emplace_back(stem.substr(start, i - start));
							start = i;
						}
					}

					ret.emplace_back(stem.substr(start));
					
					for (auto& part : ret) {
						trim(part); // Trim leading and trailing whitespace.
					}

					return ret;
				}(stem) };
				
				for (auto&& part : parts) {
					if (part.empty()) {
						continue;
					}
					
					if (part[0] != u8'#') { // Not empty so safe to check.
						type = std::move(part); // If it doesn't start with #, it's the type (first part).
					}
					else {
						// Here, first character is '#'. We expect another character, and then an '=', and then optionally whatever else.
						
						const auto eq_idx = part.find(u8'=');
						
						if (eq_idx >= part.length()) {
							continue;
						}
						
						u8string tag{ part.substr(1, eq_idx - 1) }; // Take from after the '#' to before the '=', e.g. part: "# C = zxc fgh" => tag: " C ". -1 is safe because [0] is '#'.
						trim(tag);									// The arithmetic with indices above is also safe because both '#' and '=' are ascii characters and take 1 byte in utf-8 too.
						
						if (tag.length() != 1) {
							continue; // Tags are expected to be single characters.
						}
						
						u8string val{ part.substr(eq_idx + 1) }; // +1 is safe because it will yield at most .length(), which is valid as substr() argument (returns empty string).
						trim(val);
						
						if ((tag[0] == u8'V') bitor (tag[0] == u8'v')) {
							variant = std::move(val);
						}
						else if ((tag[0] == u8'I') bitor (tag[0] == u8'i')) {
							version = std::move(val);
						}
						else if ((tag[0] == u8'C') bitor (tag[0] == u8'c')) {
							catalog = std::move(val);
						}
					}
				}
			}
			
			u8string type{ u8"N/A" };
			u8string variant{ u8"N/A" };
			u8string version{ u8"N/A" };
			u8string catalog{ u8"N/A" };
		};
		
	public:
		
		[[nodiscard]] constexpr bool reserve(const std::size_t n) noexcept {
			try {
				str.reserve(n);
				return true;
			}
			catch (...) {
				return false;
			}
		}
		
		[[nodiscard]] constexpr std::size_t length() const noexcept { return str.length(); }
		
		[[nodiscard]] constexpr bool empty() const noexcept { return str.empty(); }
		
		[[nodiscard]] bool append(const file& f) noexcept {
			try {
				const u8string u8ogparent{ f.original_path.parent_path().u8string() };
				
				// Update last parent. 
				if (f.parent != last_parent) {					// Input file is in a different folder from the previous one.
					last_parent = f.parent;						// Update last parent.
					str.append(u8ogparent).append(u8"\r\n");	// Append the parent string (no tab). Use og for capitalization.
				}
				
				// Append filename.
				str.append(u8"\t")
				   .append(f.original_path.filename().u8string())
				   .append(u8"\r\n");	
				
				auto parents{ split(u8ogparent, u8'\\') };
				
				const u8string standard{ (parents.size() > 0) ? std::move(parents[0]) : u8"N/A"s };
				const u8string family{ (parents.size() > 1) ? std::move(parents[1]) : u8"N/A"s };
				
				name_attrs attrs{ f.original_path.stem().u8string() };
				
				// Append details.
				str.append(u8"\t\tStandard: ").append(standard)
				   .append(u8"\r\n\t\tFamily: ").append(family)
				   .append(u8"\r\n\t\tType: ").append(attrs.type)
				   .append(u8"\r\n\t\tVariant: ").append(attrs.variant)
				   .append(u8"\r\n\t\tVersion: ").append(attrs.version)
				   .append(u8"\r\n\t\tCatalog: ").append(attrs.catalog)
				   .append(u8"\r\n\t\tOwner: ").append(f.owner.val)
				   .append(u8"\r\n");
				
				return true;
			}
			catch (...) {
				return false;
			}
		}
		
		u8string str{};
		lowercase_path last_parent{};
	};
	
	
	std::optional<u8string> diff_sorted_files(const old_files_t& olds, const new_files_t& news) noexcept {

		diff_string_maker created{};
		diff_string_maker deleted{};
		// diff_string_maker changed{};
		
		// if (not (created.reserve(1024) and deleted.reserve(1024) and changed.reserve(1024))) { // Start off with a big block to avoid initial growth's allocation spam.
		if (not (created.reserve(1024) and deleted.reserve(1024))) { // Start off with a big block to avoid initial growth's allocation spam.
			log::error("Diffing: Failed to allocate initial space."sv);
			return std::nullopt;
		}
		
		std::size_t deleted_count = 0;
		std::size_t created_count = 0;
		std::size_t remained_count = 0;
		
		{
			// Set intersection-like
			
			auto old_it{ olds.files.begin() };
			auto new_it{ news.files.begin() };
			const auto old_end{ olds.files.end() };
			const auto new_end{ news.files.end() };
			
			while ((old_it != old_end) bitand (new_it != new_end)) {
				
				const auto cmp = (*old_it <=> *new_it); // Spaceship!
				
				if (cmp < 0) { // old < new, so this old is not present in news, so it has been deleted.
					if (not deleted.append(*old_it)) {
						log::error("Diffing: Failed to append file to \"deleted\" list."sv);
						return std::nullopt;
					}
					++old_it;
					++deleted_count;
				}
				else if (cmp > 0) { // old > new, so this new is not present in olds, so it is newly created.
					if (not created.append(*new_it)) {
						log::error("Diffing: Failed to append file to \"created\" list."sv);
						return std::nullopt;
					}
					++new_it;
					++created_count;
				}
				else { // old == new, so this new existed before and still exists.
					
					// Handling here scrapped because no longer relevant.
					
					++old_it;
					++new_it;
					++remained_count;
				}
			}
			
			// Handle remaining deleted files.
			for (; old_it != old_end; ++old_it) {
				if (not deleted.append(*old_it)) {
					log::error("Diffing: Failed to append file to \"deleted\" list."sv);
					return std::nullopt;
				}
				++deleted_count;
			}
			
			// Handle remaining created files.
			for (; new_it != new_end; ++new_it) {
				if (not created.append(*new_it)) {
					log::error("Diffing: Failed to append file to \"created\" list."sv);
					return std::nullopt;
				}
				++created_count;
			}
			
		}
		
		u8string report{};
		
		try {
			report.reserve(created.length() + deleted.length() + 100); // An extra 100 characters for headings and newlines and stuff. We need about 40. Cba doing precise calcs.
		}
		catch (...) {
			log::error("Diffing: Failed to allocate report string space."sv);
			return std::nullopt;
		}
		
		log::info("Diffing: Allocated <{}> bytes for report."sv, report.capacity());
		
		if (created.empty()) {
			report.append(u8"No new files.\r\n\r\n");
		}
		else {
			report.append(u8"New files:\r\n\r\n").append(created.str).append(u8"\r\n");
		}
		
		if (deleted.empty()) {
			report.append(u8"No files deleted.\r\n\r\n");
		}
		else {
			report.append(u8"Deleted files:\r\n\r\n").append(deleted.str);
		}
		
		log::info("Diffing: Generated report with info for <{}> deleted, <{}> created, and <{}> still-existing files."sv, deleted_count, created_count, remained_count);
		return report;
	}
	
	
}