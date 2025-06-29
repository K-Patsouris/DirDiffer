#pragma once
#include "int_defs.h"
//#include <random>
//#include <bit>

namespace diff {

	//struct random_state_tag {};

	// 64bit stack state. Very fast. Credit to Ian C. Bullard.
	class gamerand {
		// The commented functions work fine. They are just unneeded in this project.
	public:
		explicit constexpr gamerand() noexcept = default;
		constexpr gamerand(gamerand&&) noexcept = default;
		constexpr gamerand(const gamerand&) noexcept = default;
		constexpr gamerand& operator=(gamerand&&) noexcept = default;
		constexpr gamerand& operator=(const gamerand&) noexcept = default;
		constexpr ~gamerand() noexcept = default;

		explicit constexpr gamerand(const u32 state) noexcept : high{ state }, low{ state ^ magic_xor } {}
		//explicit gamerand(random_state_tag) noexcept : high{ std::random_device{}() }, low{ high ^ magic_xor } {}

		constexpr void set_state(const u32 state) noexcept {
			high = state;
			low = state ^ magic_xor;
		}
		/*void set_state(random_state_tag) noexcept {
			high = std::random_device{}();
			low = high ^ magic_xor;
		}*/

		constexpr u32 next() noexcept {
			high = (high >> u32{ 16 }) + (high << u32{ 16 });
			high += low;
			low += high;
			return high;
		}

		// [0, cap). cap must be > 0
		//constexpr u32 next(const u32 cap) noexcept { return next() % cap; }

		//constexpr float nextf01() noexcept { return std::bit_cast<float>(static_cast<u32>(sign_and_exponent | static_cast<u32>(next() >> 9))) - 1.0f; }

		//constexpr float nextf(const float min, const float max) noexcept { return min + (nextf01() * (max - min)); }

	private:
		enum : u32 {
			magic_xor = 0x49616E42,
			//sign_and_exponent = u32{ 127 } << u32{ 23 }
		};

		u32 high{ 1 };
		u32 low{ 1 ^ magic_xor };
	};


}
