#pragma once

namespace comp::game
{
	// -----------------------------------------------------------------------
	// Red Faction camera/matrix structures
	// These are placeholders — fill in once addresses are found via x32dbg.
	//
	// HOW TO FIND IN x32dbg (RF_120na.exe base = 0x00400000):
	//   1. Search for strings like "camera", "view", "frustum" in References tab
	//   2. Or: pause mid-frame, check call stack for a function that calls
	//      IDirect3DDevice8::SetTransform with D3DTS_VIEW (vtable offset 0x5C)
	//      -> walk up call stack to RF_120na.exe frame -> that function reads
	//      the view matrix from a global struct
	//   3. The projection matrix is usually set just before or after view
	//   4. Alternatively: search for float pattern "1.0 0.0 0.0 0.0" (identity)
	//      in memory scanner to find matrix pools
	// -----------------------------------------------------------------------

	// RF camera matrix struct (layout TBD via reverse engineering)
	// Likely: 4x4 view matrix followed by 4x4 projection matrix
	struct rf_camera_t
	{
		D3DXMATRIX view;
		D3DXMATRIX proj;
		// additional fields may exist (position, frustum planes, etc.)
		// pad with unknowns once struct size is determined
	};

	// RF vertex buffer entry for pre-transformed geometry (D3DFVF_XYZRHW)
	// This is what the game submits to D3D — already in screen space
	struct rf_rhw_vertex_t
	{
		float x, y, z;   // screen-space position
		float rhw;        // reciprocal homogeneous W (1/w)
		// additional fields (color, UV, etc.) may follow — determine via lookat_vertex_decl
	};

	// RF world-space vertex (what we want to reconstruct and resubmit)
	struct rf_world_vertex_t
	{
		float x, y, z;   // world-space position
		DWORD color;      // diffuse color (may not be needed)
		float u, v;       // texture coordinates
	};
}
