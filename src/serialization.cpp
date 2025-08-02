#include "serialization.h"
#include "logger.h"
#include "int_defs.h"
#include "string_defs.h"
#include "rng.h"
#include <random>
#include <array>
#include <concepts>


namespace diff {
	
	
	enum : u32 { serialization_version = 1 };

	enum class encryption : u32 { enabled , disabled };
	
	// A simple strong typedef, nameable through multicharacter literals, e.g. strong<vector<int>, 'foo'> foos;
	template<std::integral T, int ID>
	struct named {
		static_assert(sizeof(int) == 4, "Multichar literals are expected to be 4 bytes.");
		
		explicit constexpr named() noexcept = default;
		constexpr named(const named&) noexcept = default;
		constexpr named(named&&) noexcept = default;
		constexpr named& operator=(const named&) noexcept = default;
		constexpr named& operator=(named&&) noexcept = default;
		constexpr ~named() noexcept = default;
		
		template<typename U> requires(std::is_constructible_v<T, U>)
		explicit constexpr named(U&& init) noexcept : value{ std::forward<U>(init) } {}
		
		template<typename U> requires(std::is_constructible_v<T, U>)
		constexpr named& operator=(U&& rhs) noexcept {
			value = std::forward<U>(rhs);
			return *this;
		}
		

		T value{};
	};
	
	struct header {
	public:
		using unit_type = u32;
		enum : size_t { block_length = 40 };
		static_assert(block_length > 3, "Serialization header num block must be at least 4 numbers long"); // To fit the seed pattern, but I don't want to type that in the string cause RE.
		
		explicit constexpr header() noexcept = default;
		constexpr header(header&&) noexcept = default;
		constexpr header& operator=(header&&) noexcept = default;
		constexpr ~header() noexcept = default;
		constexpr header(const header&) = default;
		constexpr header& operator=(const header&) = default;
		
		explicit header(named<bool, 'ENCR'> encrypt, named<unit_type, 'SEED'> seed) noexcept
			: version{ serialization_version }
			, header_size{ sizeof(header) }
			, wchar_size{ static_cast<u32>(sizeof(wchar_t)) }
			, encryption_flag{ encrypt.value }
		{
			gamerand local_rng{ std::random_device{}() };
			
			// Randomize all block u32s.
			for (auto& num : block) {
				num = local_rng.next();
			}
			
			if (!encrypt.value) {
				return; // Done if not encrypting.
			}
			
			using iterator_t = decltype(block)::iterator;

			iterator_t seed_it = block.end();

			// Try finding an existing pattern (x-y-z-seed with x <= y <= z) in the random block data.
			{
				for (size_t i = 0; i < (block.size() - 3); ++i) {
					const u32 first = block[i];
					const u32 second = block[i + 1];
					const u32 third = block[i + 2];
					if ((first <= second) bitand (second <= third)) {
						seed_it = block.begin() + i + 3;
						//log::info("Serialization: Header: Found seed placement pattern in random data. Seed will be at position <{}>"sv, std::distance(block.begin(), seed_it));
						break;
					}
				}
			}

			if (seed_it == block.end()) { // Pattern not found. Create it somewhere randomly.
				auto simple_swap = [] (iterator_t lhs, iterator_t rhs) -> void {
					unit_type temp = *lhs;
					*lhs = *rhs;
					*rhs = temp;
				};
				// Starting index of the seed position pattern. If block_length == 40, idx <= 36, so idx + 3 is always a valid index.
				iterator_t smallest = block.begin() + (local_rng.next() % (block.size() - 3));
				iterator_t middle = smallest + 1;
				iterator_t biggest = smallest + 2;
				//log::info("Serialization: Header: Seed placement pattern not found in random data. Creating it manually. Seed will be at position <{}>"sv, std::distance(block.begin(), seed_it));
				//log::info("Serialization: Header: Pattern initial values (x-y-z-seed) = {} - {} - {} - {}."sv, *smallest, *middle, *biggest, seed.value);
				// Force order the three numbers to each be <= than the next.
				if (middle < smallest) {
					simple_swap(smallest, middle);
				}
				if (biggest < middle) {
					simple_swap(middle, biggest);
					if (middle < smallest) {
						simple_swap(smallest, middle);
					}
				}
				seed_it = smallest + 3;
				//log::info("Serialization: Header: Pattern final values (x-y-z-seed) = {} - {} - {} - {}."sv, *smallest, *middle, *biggest, seed.value);
			}

			
			// Place the seed after the ordered pattern, found or created.
			*seed_it = seed.value;

			//log::info("Serialization: Header: After seed placement, pattern final values (x-y-z-seed) = {} - {} - {} - {}."sv, *(seed_it - 3), *(seed_it - 2), *(seed_it - 1), *seed_it);
		}
		
		constexpr unit_type get_version() const noexcept { return version; }
		constexpr unit_type get_header_size() const noexcept { return header_size; }
		constexpr unit_type get_wchar_size() const noexcept { return wchar_size; }
		constexpr bool get_encrypted() const noexcept { return encryption_flag != 0; }
		constexpr std::optional<unit_type> get_seed() const noexcept {
			for (size_t i = 0; i < (block.size() - 3); ++i) {
				const u32 first = block[i];
				const u32 second = block[i + 1];
				const u32 third = block[i + 2];
				const u32 potential_seed = block[i + 3];
				if ((first <= second) and (second <= third)) {
					//log::info("Serialization: Header: Found seed at position <{}>"sv, i + 3);
					return potential_seed;
				}
			}
			return {};
		}
		
	private:
		unit_type version{};
		unit_type header_size{};
		unit_type wchar_size{};
		unit_type encryption_flag{};
		std::array<unit_type, block_length> block{};
	};
	static_assert(sizeof(header) == (sizeof(header::unit_type) * (4 + header::block_length)));
	static_assert(std::is_trivially_copyable_v<header>);


	std::optional<dynamic_buffer> serialize_to_buffer(const smtp_info& smtp, const diff::vector<file>& files, encryption encr_setting) noexcept {
		
		static_assert(sizeof(smtp_info) == (
			sizeof(decltype(smtp_info::url)) +
			sizeof(decltype(smtp_info::username)) +
			sizeof(decltype(smtp_info::password))
		),
			"Unexpected smtp_info stack size. Did you change the class but forgot to update serialization?" // These strings don't make it into the binary, so it's okay to say "email" .
		);
		
		static_assert(sizeof(file) == (
			sizeof(decltype(file::original_path)) +
			sizeof(decltype(file::parent)) +
			sizeof(decltype(file::filename)) +
			sizeof(decltype(file::owner)) +
			sizeof(decltype(file::size_in_bytes)) +
			sizeof(decltype(file::last_write))
		),
			"Unexpected file stack size. Did you change the class but forgot to update serialization?"
		);
		
		static_assert(std::is_same_v <u32, decltype(std::random_device{}())> , "Seed size unexpected.");
		
		static_assert(sizeof(std::size_t) <= sizeof(u64), "std::size_t is bigger than u64!"); // std::size_t could theoretically exceed 64bit when we get 128bit architectures.
		
		const bool encryption_enabled = encr_setting == encryption::enabled;
		
		const u32 seed = std::random_device{}(); // Always invoke random_device, regardless of whether we're actually gonna encrypt or not.
		
		//log::info("Serialization: Writing seed <{}>."sv, seed);

		const header h{ named<bool, 'ENCR'>{ encryption_enabled }, named<u32, 'SEED'>{ seed } };

		dynamic_buffer buf{};
		
		const u64 total_size{ [](const smtp_info& smtp, const diff::vector<file>& files) {
			auto string_needed_bytes = [](const auto& str) noexcept -> u64 {
				return 8u + (str.length() * sizeof(std::remove_cvref_t<decltype(str)>::value_type)); // 8 for length, then just enough for each char.
			};
			
			u64 ret = sizeof(header);
			
			// Credentials
			ret += string_needed_bytes(smtp.url);
			ret += string_needed_bytes(smtp.username);
			ret += string_needed_bytes(smtp.password);
			
			// Files
			ret += 8; // 8 bytes for vector size (file count).
			for (const auto& file : files) {
				ret += string_needed_bytes(file.original_path.native());
				ret += string_needed_bytes(file.parent.str_cref());
				ret += string_needed_bytes(file.filename.str_cref());
				ret += string_needed_bytes(file.owner.val);
				ret += sizeof(decltype(file::size_in_bytes));
				ret += sizeof(std::chrono::nanoseconds::rep);
			}
			
			return ret;
		}(smtp, files) };
		
		if (not buf.expand_for_extra(total_size)) {
			log::error("Serialization: Failed to allocate buffer space (<{}> bytes)"sv, total_size);
			return std::nullopt;
		}
		
		// Write Header
		(void)buf.write(h);
		
		// Write SMTP info
		(void)buf.write(smtp.url);
		(void)buf.write(smtp.username);
		(void)buf.write(smtp.password);
		
		// Write Files
		(void)buf.write(static_cast<u64>(files.size()));	// File count
		for (const auto& file : files) {					// Files
			(void)buf.write(file.original_path.native());
			(void)buf.write(file.parent.str_cref());
			(void)buf.write(file.filename.str_cref());
			(void)buf.write(file.owner.val);
			(void)buf.write(file.size_in_bytes);
			(void)buf.write(static_cast<i64>(file.last_write.count()));
			// rep for seconds is specified as signed 35+ bit (lol) int. Even though everyone implements it with i64, cast explicitly. This will probably be UB in 292 billion years.
		}
		
		// Encrypt if needed
		if (encryption_enabled) {
			gamerand rng{ seed };
			for (auto it{ buf.begin() + sizeof(header) }, end{ buf.end() }; it != end; ++it) {
				*it += static_cast<unsigned char>(rng.next());	// ADD a random value to every byte.
			}
		}
		
		buf.rewind();
		
		log::info("Serialization: Serialized <{}> bytes to a buffer."sv, buf.length());
		return buf;
	}
	
	std::optional<dynamic_buffer> serialization::serialize_to_buffer_encrypted(const smtp_info& smtp, const diff::vector<file>& files) noexcept {
		return serialize_to_buffer(smtp, files, encryption::enabled);
	}
	std::optional<dynamic_buffer> serialization::serialize_to_buffer_unencrypted(const smtp_info& smtp, const diff::vector<file>& files) noexcept {
		return serialize_to_buffer(smtp, files, encryption::disabled);
	}
	
	
	std::optional<serialization::simple_pair> serialization::deserialize_from_buffer(const dynamic_buffer& buf) noexcept {
		buf.rewind();
		
		// Read Header
		header h{};
		if (not buf.read(h)) {
			log::error("Deserialization: Failed to read header from buffer."sv);
			return std::nullopt;
		}
		if (const auto header_size{ h.get_header_size() }; header_size != sizeof(header)) {
			log::error("Deserialization: Unexpected header size (expected {}, read {})."sv, sizeof(header), header_size);
			return std::nullopt;
		}
		if (const auto wchar_size{ h.get_wchar_size() }; wchar_size != sizeof(wchar_t)) {
			log::error("Deserialization: Unexpected wchar size (expected {}, read {})."sv, sizeof(wchar_t), wchar_size);
			return std::nullopt;
		}
		if (const auto deserializing_version = h.get_version(); deserializing_version != serialization_version) {
			// Atm there is only version 1 so nothing to do.
			static_assert(serialization_version == 1, "New serialization version detected, but no code written to handle it.");
		}
		
		// Decrypt
		if (h.get_encrypted()) {
			log::info("Deserialization: Buffer is encrypted. Decrypting..."sv);
			if (const auto opt_seed{ h.get_seed() }; not opt_seed.has_value()) {
				log::error("Deserialization: Failed to decrypt buffer."sv);
				return std::nullopt;
			}
			else {
				//log::info("Deserialization: Read seed value <{}>"sv, opt_seed.value());
				gamerand rng{ opt_seed.value()};
				for (auto it{ buf.begin() + sizeof(header) }, end{ buf.end() }; it != end; ++it) {
					*it -= static_cast<unsigned char>(rng.next());	// SUBTRACT a random value to every byte.
				}
			}
		}
		else {
			log::info("Deserialization: Buffer is not encrypted."sv);
		}
		
		simple_pair ret{};
		
		// Read Credentials
		if (not buf.read(ret.smtp.url)
			or not buf.read(ret.smtp.username)
			or not buf.read(ret.smtp.password))
		{
			log::error("Deserialization: Failed to read SMTP info!"sv);
			return std::nullopt;
		}
		
		// Read Files
		u64 file_count = 0;
		if (not buf.read(file_count)) {
			log::error("Deserialization: Failed to read file count!"sv);
			return std::nullopt;
		}
		
		if (file_count == 0) {
			log::info("Deserialization: Deserialized misc data and zero files from buffer."sv);
			return ret;
		}
		
		try {
			ret.files.reserve(file_count);
		}
		catch (...) {
			log::error("Deserialization: Failed to allocate file vector space."sv);
			return std::nullopt;
		}
		
		for (u64 i = 0; i < file_count; ++i) {
			diff::wstring og_path{};
			diff::u8string parent{};
			diff::u8string filename{};
			diff::u8string owner{};
			u64 file_size{};
			i64 last_write{};
			if (buf.read(og_path) and
				buf.read(parent) and
				buf.read(filename) and
				buf.read(owner) and
				buf.read(file_size) and
				buf.read(last_write))
			{
				ret.files.emplace_back(
					std::filesystem::path{ std::move(og_path) },
					lowercase_path{ lowercase_path::already_lowercase_tag{}, std::move(parent) },
					lowercase_path{ lowercase_path::already_lowercase_tag{}, std::move(filename) },
					file::owner_name{ std::move(owner) },
					file_size,
					std::chrono::seconds{ last_write }
				);
			}
			else {
				log::error("Deserialization: Failed to deserialize file #{}. Aborted."sv, i + 1);
				return std::nullopt;
			}
			
		}
		
		log::info("Deserialization: Deserialized misc data and <{}> files."sv, ret.files.size());
		return ret;
	}
	
	
	
	
}
