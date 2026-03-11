#include "std_include.hpp"
#include "shared/common/flags.hpp"

namespace comp::game
{
	// -----------------------------------------------------------------------
	// Red Faction address definitions
	// Fill these in once found via x32dbg
	// -----------------------------------------------------------------------

	rf_camera_t* g_rf_camera = nullptr;

	// TODO: Replace 0x0 with real addresses found via x32dbg
	// See game.hpp for detailed instructions on how to find each address
	uint32_t hk_addr__rf_submit_geometry   = 0x0; // hook: start of geometry submit func
	uint32_t retn_addr__rf_submit_geometry = 0x0; // return: instruction after hook bytes
	uint32_t hk_addr__rf_set_view_matrix   = 0x0; // hook: RF SetViewMatrix func
	uint32_t retn_addr__rf_set_view_matrix = 0x0; // return: instruction after hook bytes

	// -----------------------------------------------------------------------

#define PATTERN_OFFSET_SIMPLE(var, pattern, byte_offset, static_addr) \
		if (const auto offset = shared::utils::mem::find_pattern(##pattern, byte_offset, #var, use_pattern, static_addr); offset) { \
			(var) = offset; found_pattern_count++; \
		} total_pattern_count++;

#define PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(var, type, pattern, byte_offset, static_addr) \
		if (const auto offset = shared::utils::mem::find_pattern(##pattern, byte_offset, #var, use_pattern, static_addr); offset) { \
			(var) = (type)*(DWORD*)offset; found_pattern_count++; \
		} total_pattern_count++;

	void init_game_addresses()
	{
		const bool use_pattern = !shared::common::flags::has_flag("no_pattern");
		if (use_pattern) {
			shared::common::log("Game", "Getting offsets ...", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
		}

		std::uint32_t total_pattern_count = 0u;
		std::uint32_t found_pattern_count = 0u;


#pragma region GAME_VARIABLES

		// RF Camera struct pointer
		// TODO: Once found via x32dbg, replace "? ? ? ? ?" with the byte pattern
		//       of the instruction that loads the camera pointer, and set the
		//       correct byte_offset and static_addr.
		//
		// Example: if x32dbg shows "mov eax, [0x00A1B2C3]" loading the camera ptr:
		//   pattern = "A1 C3 B2 A1 00"  (A1 = mov eax, [addr]; addr in LE bytes)
		//   byte_offset = 1              (skip the A1 opcode to get to the address)
		//   static_addr = 0x00A1B2C3    (the direct address for -no_pattern mode)
		//
		// PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(g_rf_camera, rf_camera_t*, "? ? ? ? ?", 1, 0xDEADBEEF);

		// end GAME_VARIABLES
#pragma endregion

		// ---

#pragma region GAME_ASM_OFFSETS

		// RF geometry submit hook address
		// TODO: Once found, replace pattern and offsets
		// PATTERN_OFFSET_SIMPLE(hk_addr__rf_submit_geometry,   "? ? ? ? ?", 0, 0xDEADBEEF);
		// PATTERN_OFFSET_SIMPLE(retn_addr__rf_submit_geometry, "? ? ? ? ?", 0, 0xDEADBEEF);

		// RF set view matrix hook address
		// TODO: Once found, replace pattern and offsets
		// PATTERN_OFFSET_SIMPLE(hk_addr__rf_set_view_matrix,   "? ? ? ? ?", 0, 0xDEADBEEF);
		// PATTERN_OFFSET_SIMPLE(retn_addr__rf_set_view_matrix, "? ? ? ? ?", 0, 0xDEADBEEF);

		// end GAME_ASM_OFFSETS
#pragma endregion


		if (use_pattern)
		{
			if (found_pattern_count == total_pattern_count) {
				shared::common::log("Game", std::format("Found all '{:d}' Patterns.", total_pattern_count), shared::common::LOG_TYPE::LOG_TYPE_GREEN, true);
			}
			else
			{
				shared::common::log("Game", std::format("Only found '{:d}' out of '{:d}' Patterns.", found_pattern_count, total_pattern_count), shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
				shared::common::log("Game", ">> Please create an issue on GitHub and attach this console log and information about your game (version, platform etc.)\n", shared::common::LOG_TYPE::LOG_TYPE_STATUS, true);
			}
		}
	}

#undef PATTERN_OFFSET_SIMPLE
#undef PATTERN_OFFSET_DWORD_PTR_CAST_TYPE

}
