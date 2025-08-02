#include "configuration.h"
#include "logger.h"
#include "string_utils.h"		// split, make_lowercase, ul_parse


namespace diff {
	
	// static_assert(false, "lookup template parsing needs rewrite");
	std::optional<configuration> configuration::parse_file_contents(const diff::u8string& contents) noexcept {
		
		struct line {
			enum value_of : u32 {
				none,
				
				root,
				file_extensions,
				excluded_folders,
				min_depth,
				email_from,
				email_to,
				email_cc,
				email_subject,
				
				invalid
			};
			
			diff::u8string str{};
			value_of category{};
			u32 source_line{};
		};
		
		diff::vector<line> lines = [](const diff::u8string& str) {
			struct str_and_source_line {
				diff::u8string str{};
				u32 source_line{};
			};
			
			diff::vector<str_and_source_line> raw_lines{ [](const diff::u8string& str) {
				auto raw_lines = split(str, u8'\n');											// Split on Unix newline char.
				diff::vector<str_and_source_line> ret{};
				ret.reserve(raw_lines.size());
				for (std::size_t i = 0; i < raw_lines.size(); ++i) {
					trim(raw_lines[i]);
					ret.emplace_back(std::move(raw_lines[i]), static_cast<u32>(i + 1));
				}
				return ret;
			}(str) };
			
			std::erase_if(raw_lines, [](const auto& val) { return val.str.empty(); });			// Remove empty lines.
			for (auto& val : raw_lines) {														// Remove carriage returns, if any were mixed in (or Windows). No empty lines here so back() is safe.
				if (val.str.back() == u8'\r') {
					val.str.pop_back();																
				}
			}
			std::erase_if(raw_lines, [](const auto& val) { return val.str.empty(); });			// Remove all lines that only had the carriage return remaining and now are empty.
			std::erase_if(raw_lines, [](const auto& val) { return val.str.length() >= 2
																and val.str[0] == u8'/'
																and val.str[1] == u8'/'; });	// Remove all comments (not using ini comment syntax because ';' is a valid path character).
			
			diff::vector<line> ret{};
			
			line::value_of current_category = line::none; // Start from invalid, to properly mark values that come at the start under no category.
			
			for (auto& val : raw_lines) {
				if ((val.str.front() == u8'<') bitand (val.str.back() == u8'>')) {
					make_lowercase(val.str);
					if (val.str == u8"<root>")					{ current_category = line::value_of::root; }
					else if (val.str == u8"<file extensions>")	{ current_category = line::value_of::file_extensions; }
					else if (val.str == u8"<excluded folders>")	{ current_category = line::value_of::excluded_folders; }
					else if (val.str == u8"<min depth>")		{ current_category = line::value_of::min_depth; }
					else if (val.str == u8"<email from>")		{ current_category = line::value_of::email_from; }
					else if (val.str == u8"<email to>")			{ current_category = line::value_of::email_to; }
					else if (val.str == u8"<email cc>")			{ current_category = line::value_of::email_cc; }
					else if (val.str == u8"<email subject>")	{ current_category = line::value_of::email_subject; }
					else										{ current_category = line::value_of::invalid; }
				}
				else {
					ret.emplace_back(std::move(val.str), current_category, val.source_line);
				}
			}
			
			return ret;
		}(contents);
		
		if (lines.empty()) {
			log::error("Config Parse: Empty config file."sv);
			return std::nullopt;
		}
		
		// <root>				SINGLE
		// <file extensions>	MULTIPLE
		// <excluded folders>	MULTIPLE	OPTIONAL
		// <min depth>			SINGLE
		// <email from>			SINGLE
		// <email to>			SINGLE
		// <email cc>			MULTIPLE	OPTIONAL
		// <email subject>		SINGLE		OPTIONAL
		
		configuration ret{};
		
		bool root_found = false;
		bool depth_found = false;
		bool from_found = false;
		bool to_found = false;
		bool subject_found = false;
		
		bool extensions_found = false;
		
		for (auto& ln : lines) {

			auto is_valid_email = [] (u8string_view email) -> bool {

				// Find '@' symbol.
				const auto at_index = email.find(u8'@');
				if (at_index >= email.length()) {
					return false;
				}

				// Ensure there is exactly one '@' symbol.
				if (const auto last_at_index = email.rfind(u8'@'); at_index != last_at_index) {
					return false;
				}

				// Ensure there are characters before and after the '@' symbol.
				if ((at_index == 0) or (at_index == email.length() - 1)) {
					return false;
				}

				u8string_view local_part{ email.begin(), email.begin() + at_index }; // [first, last). Take everything before the '@'.

				// Ensure local-part does not start or end with '.'.
				if ((local_part.front() == u8'.') or (local_part.back() == u8'.')) {
					return false;
				}

				// Ensure local-part does not start or end with '_'.
				if ((local_part.front() == u8'_') or (local_part.back() == u8'_')) {
					return false;
				}

				// Ensure local-part does not contain consecutive '.'s.
				for (std::size_t i = 0; i < local_part.length() - 1; ++i) {
					if ((local_part[i] == u8'.') bitand (local_part[i + 1] == u8'.')) {
						return false;
					}
				}

				auto is_alphanumeric = [] (const char8_t u8c) {
					const char c = static_cast<char>(u8c);
					const bool is_digit = (c >= '0') bitand (c <= '9');
					const bool is_letter = ((c >= 'A') bitand (c <= 'Z')) bitor ((c >= 'a') bitand (c <= 'z'));
					return is_digit bitor is_letter;
				};

				// Ensure local-part consists only of numbers, letters, and dots.
				for (const auto u8c : local_part) {
					if (not ((u8c == u8'.') bitor (u8c == u8'_') bitor is_alphanumeric(u8c))) {
						return false;
					}
				}

				u8string_view domain{ email.begin() + at_index + 1, email.end() }; // [first, last). Take everything after the '@'.

				const auto domain_parts{ split(domain, u8'.') };

				// Ensure domain has at least 2 parts, separated by a '.'.
				if (domain_parts.size() < 2) {
					return false;
				}

				for (const auto& part : domain_parts) {
					
					// Ensure no domain parts are empty (no consecutive dots allowed).
					if (part.empty()) {
						return false;
					}

					// Ensure no domain parts start OR end in '-'.
					if ((part.front() == u8'-') bitor (part.back() == u8'-')) {
						return false;
					}

					// Ensure domain parts only contain numbers, letters, and hyphens.
					for (const auto u8c : part) {
						if (not ((u8c == u8'-') bitor is_alphanumeric(u8c))) {
							return false;
						}
					}
				}

				// Ensure top-level domain does not consist only of numbers.
				const auto toplevel_num_count = std::count_if(domain_parts.back().begin(), domain_parts.back().end(), [] (const char8_t u8c) {
					const char c = static_cast<char>(u8c);
					return (c >= '0') bitand (c <= '9');
				});
				if (static_cast<std::size_t>(toplevel_num_count) == domain_parts.back().size()) {
					return false;
				}

				// Valid email address!
				return true;
			};

			switch (ln.category) {
			case line::none: {
				log::warning("Config Parse: Syntax error at line <{}>. Value is under no category and was ignored."sv, ln.source_line);
				break;
			}
			case line::root: {
				if ((ln.str.back() == u8'\\') bitor (ln.str.back() == u8'/')) {
					ln.str.pop_back();
				}
				ret.root = ln.str;
				if (root_found) {
					log::warning("Config Parse: Definition of <root> at line <{}> overrides previous one."sv, ln.source_line);
				}
				root_found = true;
				break;
			}
			case line::file_extensions: {
				if (ln.str.front() != u8'.') {
					ln.str = u8'.' + ln.str;
				}
				lowercase_path ext{ std::move(ln.str) };
				if (std::find(ret.extensions.begin(), ret.extensions.end(), ext) != ret.extensions.end()) {
					log::warning("Config Parse: Duplicate extension at line <{}> was ignored."sv, ln.source_line);
				}
				else {
					ret.extensions.emplace_back(std::move(ext));
					extensions_found = true;
				}
				break;
			}
			case line::excluded_folders: {
				if ((ln.str.back() == u8'\\') bitor (ln.str.back() == u8'/')) {
					ln.str.pop_back();
				}
				if ((ln.str.front() == u8'\\') bitor (ln.str.front() == u8'/')) {
					ln.str.erase(ln.str.begin());
				}
				lowercase_path excl{ std::move(ln.str) };
				if (std::find(ret.excluded_folders.begin(), ret.excluded_folders.end(), excl) != ret.excluded_folders.end()) {
					log::warning("Config Parse: Duplicate excluded folder at line <{}> was ignored."sv, ln.source_line);
				}
				else {
					ret.excluded_folders.emplace_back(std::move(excl));
				}
				break;
			}
			case line::min_depth: {
				if (const i64 parsed = ul_parse(ln.str); parsed < 0) {
					log::warning("Config Parse: Could not parse <min depth> value at line <{}> as number."sv, ln.source_line);
				}
				else {
					ret.min_depth = static_cast<u32>(parsed);
					if (depth_found) {
						log::warning("Config Parse: Definition of <min depth> at line <{}> overrides previous one."sv, ln.source_line);
					}
					depth_found = true;
				}
				break;
			}
			case line::email_from: {
				if (not is_valid_email(ln.str)) {
					log::warning("Config Parse: Invalid <email from> address at line <{}> was ignored."sv, ln.source_line);
				}
				else {
					ret.email.from = ln.str;
					if (from_found) {
						log::warning("Config Parse: Definition of <email from> at line <{}> overrides previous one."sv, ln.source_line);
					}
					from_found = true;
				}
				break;
			}
			case line::email_to: {
				if (not is_valid_email(ln.str)) {
					log::warning("Config Parse: Invalid <email to> address at line <{}> was ignored."sv, ln.source_line);
				}
				else {
					ret.email.to = ln.str;
					if (to_found) {
						log::warning("Config Parse: Definition of <email to> at line <{}> overrides previous one."sv, ln.source_line);
					}
					to_found = true;
				}
				break;
			}
			case line::email_cc: {
				if (std::find_if(ret.email.cc.begin(), ret.email.cc.end(),
														[&] (const auto& s) { return u8_iequal(s, ln.str); }
				) != ret.email.cc.end()) {
					log::warning("Config Parse: Duplicate Cc at line <{}> was ignored."sv, ln.source_line);
				}
				else {
					if (not is_valid_email(ln.str)) {
						log::warning("Config Parse: Invalid Cc email address at line <{}> was ignored."sv, ln.source_line);
					}
					else {
						ret.email.cc.emplace_back(std::move(ln.str));
					}
				}
				break;
			}
			case line::email_subject: {
				ret.email.subject = ln.str;
				if (subject_found) {
					log::warning("Config Parse: Definition of <email subject> at line <{}> overrides previous one."sv, ln.source_line);
				}
				subject_found = true;
				break;
			}
			case line::invalid: {
				log::error("Config Parse: Value at line <{}> belongs to an invalid category and is ignored."sv);
				break;
			}
			}
		}
		
		if (not root_found) {
			log::error("Config Parse: <root> was not specified. Parse failed."sv);
			return std::nullopt;
		}
		if (not depth_found) {
			log::error("Config Parse: <min depth> was not specified. Parse failed."sv);
			return std::nullopt;
		}
		if (not from_found) {
			log::error("Config Parse: <email from> was not specified. Parse failed."sv);
			return std::nullopt;
		}
		if (not to_found) {
			log::error("Config Parse: <email to> (primary recipient) was not specified. Parse failed."sv);
			return std::nullopt;
		}
		// Not checking subject_found as subject can be left empty.
		
		if (not extensions_found) {
			log::error("Config Parse: No extensions were specified under the <file extensions> category. Parse failed."sv);
			return std::nullopt;
		}
		
		// TODO
		// Exception checks
		// Path validating (root, excludeds)
		// Extension validating
		// Email address validating
		
		return ret;
	}
	
	
	bool configuration::folder_is_excluded(const lowercase_path& folder_path) const noexcept {
		for (const auto& excl : excluded_folders) {
			if (folder_path == excl) {
				return true; // Verbatim match.
			}

			const auto& excl_ref = excl.str_cref();
			const auto& in_ref = folder_path.str_cref();

			if ((excl_ref.length() < in_ref.length()) and (in_ref.find_first_of(excl_ref) == 0)) {
				const auto char_after_excl = in_ref[excl_ref.length()];
				if ((char_after_excl == u8'\\') bitor (char_after_excl == u8'/')) {
					return true; // Input is a subdirectory of this excluded folder.
				}
			}
			
		}
		return false;
	}

	bool configuration::ext_is_accepted(const lowercase_path& ext) const noexcept {
		if (extensions.empty()) {
			return true;
		}
		for (const auto& accepted : extensions) {
			if (ext == accepted) {
				return true;
			}
		}
		return false;
	}

	diff::u8string configuration::dump() const {
		diff::u8string ret{ u8"Configuration dump:\n" };

		ret += u8"\tRoot: <" + root.u8string() + u8">\n";

		const std::string asd = std::to_string(min_depth);
		diff::u8string u8asd{};
		u8asd.resize(asd.length());
		std::memcpy(u8asd.data(), asd.c_str(), asd.length());

		ret += u8"\tMinimum Depth: <" + u8asd + u8">\n";

		ret += u8"\tExtensions:\n";
		for (const auto& ext : extensions) {
			ret += u8"\t\t" + ext.str_cref() + u8'\n';
		}

		ret += u8"\tExcluded Folders:\n";
		for (const auto& fld : excluded_folders) {
			ret += u8"\t\t" + fld.str_cref() + u8'\n';
		}

		ret += u8"\tEmail Sender: <" + email.from + u8">\n";

		ret += u8"\tEmail Sender: <" + email.to + u8">\n";

		ret += u8"\tEmail Sender: <" + email.subject + u8">\n";

		ret += u8"\tEmail Cc:\n";
		for (const auto& cc : email.cc) {
			ret += u8"\t\t" + cc + u8'\n';
		}

		return ret;
	}


}