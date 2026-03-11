#include "std_include.hpp"
#include "renderer.hpp"

#include "imgui.hpp"
#include "game/game.hpp"

namespace comp
{
	int g_is_rendering_something = 0;
	bool g_rendered_first_primitive = false;
	bool g_applied_hud_hack = false; // was hud "injection" applied this frame
	bool g_rf_in_geometry_submit = false; // true while RF geometry submit func is executing

	namespace tex_addons
	{
		bool initialized = false;
		LPDIRECT3DTEXTURE9 berry = nullptr;

		void init_texture_addons(bool release)
		{
			if (release)
			{
				if (tex_addons::berry) tex_addons::berry->Release();
				return;
			}

			shared::common::log("Renderer", "Loading CompMod Textures ...", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);

			auto load_texture = [](IDirect3DDevice9* dev, const char* path, LPDIRECT3DTEXTURE9* tex)
				{
					HRESULT hr;
					hr = D3DXCreateTextureFromFileA(dev, path, tex);
					if (FAILED(hr)) shared::common::log("Renderer", std::format("Failed to load {}", path), shared::common::LOG_TYPE::LOG_TYPE_ERROR, true);
				};

			const auto dev = shared::globals::d3d_device;
			load_texture(dev, "rtx_comp\\textures\\berry.png", &tex_addons::berry);
			tex_addons::initialized = true;
		}
	}


	// ----

	drawcall_mod_context& setup_context(IDirect3DDevice9* dev)
	{
		auto& ctx = renderer::dc_ctx;
		ctx.info.device_ptr = dev;

		// any additional info about the current drawcall here

		return ctx;
	}

	// RF: Check if the current FVF uses pre-transformed (screen-space) vertices
	bool is_rhw_draw(IDirect3DDevice9* dev)
	{
		DWORD fvf = 0;
		dev->GetFVF(&fvf);
		return (fvf & D3DFVF_XYZRHW) != 0;
	}

	// RF: Reconstruct world-space draw call from RHW (screen-space) vertices
	// Uses the stored g_rf_camera view+proj matrices to un-project back to world space,
	// then resubmits the draw call with proper 3D vertices so RTX Remix can ray trace them.
	HRESULT reconstruct_world_draw(IDirect3DDevice9* dev, const D3DPRIMITIVETYPE& PrimitiveType,
		const UINT& StartVertex, const UINT& PrimitiveCount,
		const INT& BaseVertexIndex, const UINT& MinVertexIndex,
		const UINT& NumVertices, const UINT& startIndex, const UINT& primCount,
		bool is_indexed)
	{
		const auto cam = game::g_rf_camera;
		if (!cam) {
			// Camera not found yet — pass through unmodified
			if (is_indexed) {
				return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			}
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		// Get current stream 0 vertex buffer
		IDirect3DVertexBuffer9* pVB = nullptr;
		UINT stride = 0, offset = 0;
		dev->GetStreamSource(0, &pVB, &offset, &stride);

		if (!pVB || stride < sizeof(game::rf_rhw_vertex_t)) {
			if (pVB) pVB->Release();
			if (is_indexed) {
				return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			}
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		// Compute how many vertices to read based on primitive type
		UINT vertCount = 0;
		if (is_indexed)
		{
			vertCount = NumVertices;
		}
		else
		{
			switch (PrimitiveType)
			{
			case D3DPT_POINTLIST:     vertCount = PrimitiveCount; break;
			case D3DPT_LINELIST:      vertCount = PrimitiveCount * 2; break;
			case D3DPT_LINESTRIP:     vertCount = PrimitiveCount + 1; break;
			case D3DPT_TRIANGLELIST:  vertCount = PrimitiveCount * 3; break;
			case D3DPT_TRIANGLESTRIP: vertCount = PrimitiveCount + 2; break;
			case D3DPT_TRIANGLEFAN:   vertCount = PrimitiveCount + 2; break;
			default:                  vertCount = PrimitiveCount + 2; break;
			}
		}

		if (vertCount == 0) {
			pVB->Release();
			if (is_indexed) {
				return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			}
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		// Lock vertex buffer and read RHW vertices (lock entire buffer from start)
		void* pData = nullptr;
		if (FAILED(pVB->Lock(0, 0, &pData, D3DLOCK_READONLY))) {
			pVB->Release();
			if (is_indexed) {
				return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			}
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		// Compute combined inverse view-projection to un-project screen-space -> world-space
		D3DXMATRIX viewProj, invViewProj;
		D3DXMatrixMultiply(&viewProj, &cam->view, &cam->proj);
		D3DXMatrixInverse(&invViewProj, nullptr, &viewProj);

		// Get viewport for screen->NDC conversion
		D3DVIEWPORT9 vp{};
		dev->GetViewport(&vp);

		// Build reconstructed world-space vertices
		// Note: src accounts for the stream offset and StartVertex for non-indexed draws.
		// For indexed draws, the index buffer selects vertices relative to BaseVertexIndex.
		std::vector<game::rf_world_vertex_t> worldVerts(vertCount);
		const auto* src = static_cast<const BYTE*>(pData) + offset
			+ static_cast<size_t>(is_indexed ? 0 : StartVertex) * stride;

		for (UINT i = 0; i < vertCount; ++i)
		{
			const auto* rhv = reinterpret_cast<const game::rf_rhw_vertex_t*>(src + i * stride);

			// Screen-space -> NDC: x in [-1,1], y in [-1,1] (flip Y), z in [0,1]
			const float ndcX = (rhv->x / static_cast<float>(vp.Width))  * 2.0f - 1.0f;
			const float ndcY = 1.0f - (rhv->y / static_cast<float>(vp.Height)) * 2.0f;
			const float ndcZ = rhv->z;

			// Un-project through inverse view-projection
			D3DXVECTOR4 clipPos(ndcX, ndcY, ndcZ, 1.0f);
			D3DXVECTOR4 worldPos;
			D3DXVec4Transform(&worldPos, &clipPos, &invViewProj);

			if (fabsf(worldPos.w) > 1e-7f) {
				worldPos.x /= worldPos.w;
				worldPos.y /= worldPos.w;
				worldPos.z /= worldPos.w;
			}

			worldVerts[i].x = worldPos.x;
			worldVerts[i].y = worldPos.y;
			worldVerts[i].z = worldPos.z;
			worldVerts[i].color = D3DCOLOR_COLORVALUE(1, 1, 1, 1);
			worldVerts[i].u = 0.0f;
			worldVerts[i].v = 0.0f;
		}

		pVB->Unlock();
		pVB->Release();

		// Create a temporary dynamic vertex buffer for the reconstructed vertices
		IDirect3DVertexBuffer9* pTempVB = nullptr;
		const UINT tempVBSize = vertCount * sizeof(game::rf_world_vertex_t);
		if (FAILED(dev->CreateVertexBuffer(tempVBSize, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
			D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPOOL_DEFAULT, &pTempVB, nullptr)))
		{
			if (is_indexed) {
				return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			}
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		void* pTempData = nullptr;
		if (FAILED(pTempVB->Lock(0, tempVBSize, &pTempData, D3DLOCK_DISCARD)))
		{
			pTempVB->Release();
			if (is_indexed) {
				return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
			}
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}
		memcpy(pTempData, worldVerts.data(), tempVBSize);
		pTempVB->Unlock();

		// Set camera matrices so RTX Remix sees a valid view/projection
		dev->SetTransform(D3DTS_WORLD, &shared::globals::IDENTITY);
		dev->SetTransform(D3DTS_VIEW, &cam->view);
		dev->SetTransform(D3DTS_PROJECTION, &cam->proj);

		// Override FVF and stream with reconstructed world-space vertices
		dev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
		dev->SetStreamSource(0, pTempVB, 0, sizeof(game::rf_world_vertex_t));

		HRESULT hr = S_OK;
		if (is_indexed) {
			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
		} else {
			hr = dev->DrawPrimitive(PrimitiveType, 0, PrimitiveCount);
		}

		// Restore original stream source and FVF
		dev->SetStreamSource(0, nullptr, 0, 0);
		dev->SetFVF(0);
		pTempVB->Release();

		return hr;
	}


	// ----

	HRESULT renderer::on_draw_primitive(IDirect3DDevice9* dev, const D3DPRIMITIVETYPE& PrimitiveType, const UINT& StartVertex, const UINT& PrimitiveCount)
	{
		// Wait for the first rendered prim before further init of the comp mod 
		if (!g_rendered_first_primitive) {
			g_rendered_first_primitive = true;
		}

		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
		}

		static auto im = imgui::get();
		im->m_stats._drawcall_prim_incl_ignored.track_single();

		auto& ctx = setup_context(dev);

		// RF: Detect and handle RHW (pre-transformed) draw calls
		if (is_rhw_draw(dev))
		{
			im->m_stats._drawcall_rhw_detected.track_single();

			if (im->m_rf_rhw_bypass_enabled && game::g_rf_camera != nullptr)
			{
				return reconstruct_world_draw(dev, PrimitiveType, StartVertex, PrimitiveCount,
					0, 0, 0, 0, 0, false);
			}
		}

		// use any logic to conditionally set this to disable the vertex shader and use fixed function fallback
		bool render_with_ff = false;

		// use fixed function fallback if true
		if (render_with_ff)
		{
			ctx.save_vs(dev);
			dev->SetVertexShader(nullptr);
		}


		// ---------
		// draw

		auto hr = S_OK;

		// do not render next surface if set
		if (!ctx.modifiers.do_not_render) 
		{
			hr = dev->DrawPrimitive(PrimitiveType, StartVertex, PrimitiveCount);
			im->m_stats._drawcall_prim.track_single();

			if (!render_with_ff) {
				im->m_stats._drawcall_using_vs.track_single();
			}
		}


		// ---------
		// post draw

		ctx.restore_all(dev);
		ctx.reset_context();

		return hr;
	}


	// ----

	HRESULT renderer::on_draw_indexed_prim(IDirect3DDevice9* dev, const D3DPRIMITIVETYPE& PrimitiveType, const INT& BaseVertexIndex, const UINT& MinVertexIndex, const UINT& NumVertices, const UINT& startIndex, const UINT& primCount)
	{
		if (!is_initialized() || shared::globals::imgui_is_rendering) {
			return dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
		}

		auto& ctx = setup_context(dev);
		const auto im = imgui::get();

		// use any logic to conditionally set this to disable the vertex shader and use fixed function fallback
		bool render_with_ff = false;

		// any drawcall modifications in here
		if (!shared::globals::imgui_is_rendering)
		{
			im->m_stats._drawcall_indexed_prim_incl_ignored.track_single();

			// if we set do_not_render somewhere before the actual drawcall -> do not draw and reset context
			if (ctx.modifiers.do_not_render) 
			{
				ctx.restore_all(dev);
				ctx.reset_context();
				return S_OK;
			}


			// RF: Detect and handle RHW (pre-transformed) draw calls
			if (is_rhw_draw(dev))
			{
				im->m_stats._drawcall_rhw_detected.track_single();

				if (im->m_rf_rhw_bypass_enabled && game::g_rf_camera != nullptr)
				{
					ctx.restore_all(dev);
					ctx.reset_context();
					return reconstruct_world_draw(dev, PrimitiveType, 0, 0,
						BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount, true);
				}
			}


			// uncomment and debug into this to see vertex format of current drawcall
				// shared::utils::lookat_vertex_decl(dev);


			// Eg: render with fixed function if 'g_is_rendering_something' is true 
				//render_with_ff = g_is_rendering_something;


			// Eg: some condition to not render the next drawcall
				//if (your_condition) {
				//	ctx.modifiers.do_not_render = true;
				//}


			// If going from shader to fixed function, you'll need at least the world transformation matrix of the current mesh (given that you set the view and projection matrices in comp::on_begin_scene_cb())
			// If you found or figured out a memory address of a structure or matrices and want to render something via FF:
				//dev->SetTransform(D3DTS_WORLD, game::current_mesh_world_transform_matrix);


			// Some games might also modify the view/projection for certain meshes (eg. first person objects), so its not a bad idea to reset the view and proj matrices every time
			// Eg: If you found a structure in memory and setup the offset and the struct:
				//if (const auto cam = game::g_rf_camera; cam)
				//{
				//	dev->SetTransform(D3DTS_VIEW, &cam->view);
				//	dev->SetTransform(D3DTS_PROJECTION, &cam->proj);
				//}


			// Identified some problematic drawcall with eg. blending issues?
			// Modify Renderstates but make sure to save them before doing so to not affect later drawcalls
				/*if (your_condition) 
				{
					ctx.save_rs(dev, D3DRS_ALPHABLENDENABLE);
					dev->SetRenderState(D3DRS_ALPHABLENDENABLE, true);

					ctx.save_rs(dev, D3DRS_BLENDOP);
					dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);

					ctx.save_rs(dev, D3DRS_SRCBLEND);
					dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);

					ctx.save_rs(dev, D3DRS_DESTBLEND);
					dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

					ctx.save_tss(dev, D3DTSS_COLOROP);
					dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);

					ctx.save_tss(dev, D3DTSS_COLORARG1);
					dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);

					ctx.save_tss(dev, D3DTSS_ALPHAOP);
					dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);

					ctx.save_tss(dev, D3DTSS_ALPHAARG1);
					dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

					ctx.save_tss(dev, D3DTSS_ALPHAARG2);
					dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

					ctx.save_rs(dev, D3DRS_TEXTUREFACTOR);
					dev->SetRenderState(D3DRS_TEXTUREFACTOR, D3DCOLOR_COLORVALUE(0, 0, 0, 1.0f)); 
				}*/

			
			// use fixed function fallback if true
			if (render_with_ff)
			{
				ctx.save_vs(dev);
				dev->SetVertexShader(nullptr);
			}

		} // end !imgui-is-rendering


		// ---------
		// draw

		auto hr = S_OK;

		// do not render next surface if set
		if (!ctx.modifiers.do_not_render) 
		{
			hr = dev->DrawIndexedPrimitive(PrimitiveType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);

			if (!shared::globals::imgui_is_rendering) {
				im->m_stats._drawcall_indexed_prim.track_single();
			}

			if (!render_with_ff) {
				im->m_stats._drawcall_indexed_prim_using_vs.track_single();
			}
		}


		// ---------
		// post draw

		ctx.restore_all(dev);
		ctx.reset_context();
		
		return hr;
	}

	// ---

	// This can be used to manually trigger remix injection without ever needing to manually tag HUD textures
	// Can help if its hard to tag UI because it might be constantly changing - or if there is no UI
	// Call this on the first UI drawcall (you obv. need to detect that on your own via a hook or something)

	void renderer::manually_trigger_remix_injection(IDirect3DDevice9* dev)
	{
		//if (!game::is_in_game) {
		//	return;
		//}

		if (!m_triggered_remix_injection)
		{
			auto& ctx = dc_ctx;

			dev->SetRenderState(D3DRS_FOGENABLE, FALSE);

			ctx.save_vs(dev);
			dev->SetVertexShader(nullptr);
			ctx.save_ps(dev);
			dev->SetPixelShader(nullptr); // needed

			ctx.save_rs(dev, D3DRS_ZWRITEENABLE);
			dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE); // required - const bool zWriteEnabled = d3d9State().renderStates[D3DRS_ZWRITEENABLE]; -> if (isOrthographic && !zWriteEnabled)

			struct CUSTOMVERTEX
			{
				float x, y, z, rhw;
				D3DCOLOR color;
			};

			const auto color = D3DCOLOR_COLORVALUE(0, 0, 0, 0);
			const auto w = -0.49f;
			const auto h = -0.495f;

			CUSTOMVERTEX vertices[] =
			{
				{ -0.5f, -0.5f, 0.0f, 1.0f, color }, // tl
				{     w, -0.5f, 0.0f, 1.0f, color }, // tr
				{ -0.5f,     h, 0.0f, 1.0f, color }, // bl
				{     w,     h, 0.0f, 1.0f, color }  // br
			};

			dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(CUSTOMVERTEX));

			ctx.restore_vs(dev);
			ctx.restore_ps(dev);
			ctx.restore_render_state(dev, D3DRS_ZWRITEENABLE);
			m_triggered_remix_injection = true;
		}
	}


	// ---

	// RF: Assembly stub - captures view matrix pointer from register into g_rf_camera
	// Uncomment and fill in the correct register/instructions once the address is found via x32dbg.
	// The hook is installed in the renderer constructor below if hk_addr__rf_set_view_matrix != 0.
	/*__declspec(naked) void rf_capture_view_matrix_stub()
	{
		__asm
		{
			// TODO: replicate the overwritten instruction(s) at hk_addr__rf_set_view_matrix here
			// Example: mov eax, [some_camera_ptr_addr]
			// Then capture the pointer:
			// mov game::g_rf_camera, eax  (or ecx, edx, etc. - whichever reg holds the camera ptr)
			jmp game::retn_addr__rf_set_view_matrix;
		}
	}*/

	// RF: Assembly stub - sets g_rf_in_geometry_submit = true at start of geometry submit func
	// Uncomment and fill in once the address is found via x32dbg.
	/*__declspec(naked) void rf_pre_submit_geometry_stub()
	{
		__asm
		{
			// TODO: replicate the overwritten instruction(s) at hk_addr__rf_submit_geometry here
			mov g_rf_in_geometry_submit, 1;
			jmp game::retn_addr__rf_submit_geometry;
		}
	}*/

	// RF: Resets the geometry submit flag (place hook at function epilogue)
	/*__declspec(naked) void rf_post_submit_geometry_stub()
	{
		__asm
		{
			mov g_rf_in_geometry_submit, 0;
			retn 0x10; // replicate original retn instruction (adjust size as needed)
		}
	}*/

	// ---

	renderer::renderer()
	{
		p_this = this;

		// RF: Install assembly hooks if addresses have been found via x32dbg
		// Uncomment each block once the corresponding address is filled in game.cpp

		// Hook to capture the RF view matrix pointer each frame:
		//if (game::hk_addr__rf_set_view_matrix != 0x0)
		//{
		//	shared::utils::hook(game::hk_addr__rf_set_view_matrix, rf_capture_view_matrix_stub, HOOK_JUMP).install()->quick();
		//}

		// Hook to track when RF is submitting geometry (pre-transform intercept):
		//if (game::hk_addr__rf_submit_geometry != 0x0)
		//{
		//	shared::utils::hook(game::hk_addr__rf_submit_geometry, rf_pre_submit_geometry_stub, HOOK_JUMP).install()->quick();
		//}

		// -----
		m_initialized = true;
		shared::common::log("Renderer", "Module initialized.", shared::common::LOG_TYPE::LOG_TYPE_DEFAULT, false);
	}

	renderer::~renderer()
	{
		tex_addons::init_texture_addons(true);
	}
}
