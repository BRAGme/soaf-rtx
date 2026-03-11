#pragma once
#include "structs.hpp"

namespace comp::game
{
	// -----------------------------------------------------------------------
	// Red Faction - reverse engineering address placeholders
	//
	// TARGET EXE: RF_120na.exe
	// BASE ADDRESS: 0x00400000 (standard Win32 non-ASLR)
	//
	// HOW TO FIND THE CAMERA STRUCT ADDRESS (x32dbg):
	//   Method A - SetTransform hook:
	//     1. In Symbols tab, find d3d8.dll -> vtable offset 0x5C = SetTransform
	//     2. Set breakpoint: bp [d3d8_base + vtable_offset_SetTransform]
	//     3. When it hits with D3DTS_VIEW (arg1 == 2), check arg2 -> that pointer
	//        IS the view matrix. Walk up call stack to find where RF reads it from.
	//     4. The instruction "mov eax, [global_addr]" loading the matrix ptr is
	//        your camera struct pointer.
	//
	//   Method B - String search:
	//     1. References tab -> search "camera" or "render" in soaf.exe region
	//     2. Find a function referencing camera-related strings
	//     3. That function likely reads from the camera struct
	//
	//   Method C - Scan for known matrix values:
	//     1. Memory Map tab -> scan RF_120na.exe region for float sequence
	//        matching a known identity or perspective matrix pattern
	//
	// HOW TO FIND THE VERTEX BUFFER SUBMIT FUNCTION:
	//   1. Find calls to D3D8 DrawIndexedPrimitive (vtable offset 0x68 in IDirect3DDevice8)
	//   2. Pattern search in CPU for: FF 51 68 (call dword ptr [ecx+68h])
	//      or FF 52 68 (call dword ptr [edx+68h])
	//   3. The caller receives the vertex buffer pointer and count as arguments
	//   4. Hook the START of that caller to intercept pre-transform vertices
	// -----------------------------------------------------------------------

	// Camera/matrix struct pointer
	// TODO: Find via x32dbg (see instructions above)
	// Pattern to search for: the function that calls SetTransform(D3DTS_VIEW, ...)
	extern rf_camera_t* g_rf_camera;

	// Address of the function that submits geometry BEFORE pre-transformation
	// This is where we want to hook to intercept world-space vertices
	// TODO: Find via x32dbg - look for the function that:
	//   1. Takes a vertex buffer pointer + count as args
	//   2. Transforms vertices using the camera matrix
	//   3. Then calls DrawIndexedPrimitive or DrawPrimitive
	extern uint32_t hk_addr__rf_submit_geometry;   // hook address (start of submit func)
	extern uint32_t retn_addr__rf_submit_geometry; // return address after hook bytes

	// Address of the RF SetViewMatrix function (called before each frame render)
	// TODO: Find by walking up from D3D8 SetTransform(D3DTS_VIEW, ...) call
	extern uint32_t hk_addr__rf_set_view_matrix;
	extern uint32_t retn_addr__rf_set_view_matrix;

	// -----------------------------------------------------------------------

	extern void init_game_addresses();
}
