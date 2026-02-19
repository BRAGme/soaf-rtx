#pragma once
#include "structs.hpp"

namespace comp::game
{
	// --------------
	// game variables

	//extern DWORD* d3d_dev_sample_addr;
	
	//inline IDirect3DDevice9* get_d3d_device() {
	//	return reinterpret_cast<IDirect3DDevice9*>(*d3d_dev_sample_addr);
	//}

	extern some_struct_containing_matrices* vp;

	// --------------
	// SOAF-specific notes
	// The RSE engine performs CPU-side frustum culling before submitting geometry to D3D.
	// This causes some meshes to be culled incorrectly, especially at the edges of the screen.
	// TODO: Find the frustum culling routine in SOAF.exe (via x64dbg/Cheat Engine) and NOP it.
	// Target executable: SOAF.exe
	// Once found, add the address pattern here:
	// extern uint32_t nop_addr__frustum_cull;

	// --------------
	// game functions

	//typedef	void (__cdecl* SampleTemplate_t)(uint32_t arg1, uint32_t arg2);
	//	extern SampleTemplate_t SampleTemplate;


	// --------------
	// game asm offsets

	//extern uint32_t retn_addr__func1;
	//extern uint32_t nop_addr__func2;
	//extern uint32_t retn_addr__pre_draw_something;
	//extern uint32_t hk_addr__post_draw_something;

	// ---

	extern void init_game_addresses();
}
