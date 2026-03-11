<h1 align="center">RF-RTX — Red Faction RTX Remix Compatibility Mod</h1>

<br>

<div align="center" markdown="1">

A compatibility mod for **Red Faction** (`RF_120na.exe`, Volition, 2001) to enable ray tracing via NVIDIA's [RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix).

<br>

</div>


## The Problem: Pre-Transformed Vertices (D3DFVF_XYZRHW)

Red Faction submits geometry to Direct3D 8 using the `D3DFVF_XYZRHW` vertex format — vertices that are **already in 2D screen space** (pre-transformed by the CPU). RTX Remix cannot ray-trace these because all 3D world-space information has been discarded before the draw call is ever issued.

**RTX Remix needs:**
- 3D world-space vertex positions (`x, y, z` in world units)
- A real view matrix (`D3DTS_VIEW`)
- A real projection matrix (`D3DTS_PROJECTION`)

**What RF sends instead:**
- 2D screen-space positions (`x, y` in pixels, `z` depth buffer value, `rhw = 1/w`)
- No view/projection matrix — the game sets them to identity or skips them entirely

### The Fix

This mod intercepts geometry **before** pre-transformation occurs inside RF's render path, then resubmits each draw call using:
- Reconstructed world-space vertices (hooked from the game's internal geometry submit function)
- The real view + projection matrices (read from RF's camera struct in memory)

The hook point and camera struct address must be found via reverse engineering (`x32dbg`). See the **Reverse Engineering Guide** below.

<br>

## Loading Chain

```
RF_120na.exe
  └─ dxwrapper.dll       (D3D8→D3D9 conversion + loads d3d9.dll)
       └─ d3d9.dll        (RTX Remix bridge client)
            └─ dinput8.dll (Ultimate ASI Loader)
                 └─ plugins/rf-rtx.asi  ← THIS MOD
```

<br>

## Building

### Prerequisites
- Visual Studio 2022
- premake5 (included in `tools/` directory)

### Steps

1. Clone the repository:
   ```bash
   git clone --recurse-submodules https://github.com/BRAGme/soaf-rtx.git
   cd soaf-rtx
   git checkout copilot/rf-rtx
   ```

2. (Optional) Set environment variables:
   - `RF_GAME_DIR` — Path to your Red Faction installation folder
   - `RF_GAME_EXE` — Name of the game executable (e.g., `RF_120na.exe`)

3. Generate Visual Studio project files:
   ```bat
   generate-buildfiles_vs22.bat
   ```

4. Open `build/rf-rtx.sln` in Visual Studio 2022 and build.

5. The compiled `.asi` file will be placed in `build/bin/Release/plugins/rf-rtx.asi`  
   (or in `RF_GAME_DIR/plugins/` if the environment variable is set).

<br>

## Installation

1. **Install dxwrapper** (D3D8 → D3D9 conversion):
   - Download from [dxwrapper releases](https://github.com/elishacloud/dxwrapper/releases)
   - Copy `dxwrapper.dll` + `dxwrapper.ini` into your Red Faction directory
   - Configure `dxwrapper.ini` for D3D8→D3D9 mode

2. **Install RTX Remix**:
   - Copy `d3d9.dll` (RTX Remix bridge) into your Red Faction directory

3. **Install Ultimate ASI Loader**:
   - Download from [Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)
   - Rename to `dinput8.dll` and copy into your Red Faction directory

4. **Install this mod**:
   - Create a `plugins\` folder in your Red Faction directory
   - Copy `rf-rtx.asi` → `plugins\rf-rtx.asi`

5. **Launch** `RF_120na.exe` — the mod will load automatically and open a console window.

<br>

## Reverse Engineering Guide (x32dbg)

> **Target:** `RF_120na.exe` — standard Win32 non-ASLR binary, base address `0x00400000`  
> **Tool:** [x32dbg](https://x64dbg.com/) (32-bit tab)

The two addresses you need to fill in are:
- `g_rf_camera` — pointer to RF's camera struct (contains view + projection matrices)
- `g_rf_submit_fn` — address of the function that submits geometry before pre-transformation

---

### Step 0 — Setup

1. Open x32dbg. Switch to the **32-bit** tab (`x32dbg.exe`, not `x64dbg.exe`).
2. File → Open → select `RF_120na.exe`.
3. Let it hit the initial system breakpoint. Press **F9** to run until the game reaches the main menu.
4. In the **Symbols** tab, confirm `RF_120na.exe` is listed and its base is `0x00400000`.

---

### Step 1 — Find the Camera Struct (Method A: SetTransform hook)

Red Faction must call `IDirect3DDevice8::SetTransform(D3DTS_VIEW, ...)` every frame to set the view matrix. We intercept that call and trace back to where RF stores the matrix.

**IDirect3DDevice8 vtable offsets (D3D8):**
| Method | Offset |
|--------|--------|
| `SetTransform` | `+0x44` |
| `DrawPrimitive` | `+0x64` |
| `DrawIndexedPrimitive` | `+0x68` |

1. In the **CPU** tab, right-click → **Search for** → **Current Module** → **String references**. Search `d3d8`. Locate `d3d8.dll` in the Symbols tab, expand it.

2. Alternatively, use the **Memory Map** tab. Find the `d3d8.dll` region. Note its base address (e.g., `0x6A000000`).

3. In the **Command** bar at the bottom, type:
   ````
   bp <device_ptr_value>+0x44
   ```
   But first you need the device pointer. Easier method: pattern-scan for the D3D8 `SetTransform` call in RF's code.

4. **Pattern search in CPU:**
   - Right-click in the CPU disassembly → **Search for** → **All Modules** → **Command**
   - Search for: `FF 51 44` (call dword ptr [ecx+44h]) — this is a vtable call to SetTransform
   - You'll get several hits. Look for ones inside `RF_120na.exe`'s address range (`0x004xxxxx`)

5. Set a breakpoint on the first hit inside RF. Press **F9** to run. When the breakpoint hits, check:
   - `ECX` = the D3D device pointer
   - Stack `[ESP+4]` = the transform state (should be `2` = `D3DTS_VIEW`)
   - Stack `[ESP+8]` = pointer to the 4×4 view matrix (`D3DXMATRIX`)

6. Note `[ESP+8]`. That's **where the matrix is in memory right now**. In the **Memory** tab, go to that address. You're looking at the live view matrix.

7. Now walk up the call stack (**Call Stack** tab). The frame above this D3D call is inside RF. Disassemble that function. Look for the `MOV` instruction that loaded the matrix pointer — something like:
   ```asm
   mov  eax, dword ptr [0x00xxxxxx]   ; load camera struct ptr from global
   add  eax, 0x40                      ; offset to view matrix within struct
   push eax
   push 2                              ; D3DTS_VIEW
   call dword ptr [ecx+0x44]          ; IDirect3DDevice8::SetTransform
   ```
   The global address `0x00xxxxxx` is your `g_rf_camera` (or a pointer to it).

8. Right-click that address → **Follow in Dump**. Verify you see a valid 4×4 float matrix (the diagonal should be near `1.0` when the camera is at identity/rest).

---

### Step 2 — Find the Camera Struct (Method B: Memory Scanner)

If Method A is noisy, scan for the matrix values directly.

1. **Memory Map** tab → right-click the main RF module region → **Search memory region**.
2. Search for the float pattern `0x3F800000` (= `1.0f`) repeated — identity matrix rows.
3. Or, during gameplay, pause mid-frame and scan for a known perspective matrix value.  
   A typical RF perspective projection `[0][0]` value (fovX) is around `1.3` → float hex `0x3FA66666`.
4. Cross-reference hits with the call-stack method above to confirm.

---

### Step 3 — Find the Pre-Transform Submit Function

This is the function inside RF that takes a vertex buffer + count and calls `DrawPrimitive` or `DrawIndexedPrimitive`. **This is where you hook** to intercept world-space vertices before RF pre-transforms them.

1. **Search for DrawIndexedPrimitive calls** (vtable offset `+0x68`):
   - CPU → right-click → Search for → All Modules → Command
   - Pattern: `FF 51 68` (call [ecx+68h]) or `FF 52 68` (call [edx+68h])
   - Filter to hits inside `RF_120na.exe` (`0x004xxxxx`)

2. Set a breakpoint on each hit. Press **F9**, let the game render a frame.

3. When the breakpoint fires, look at the **Call Stack**. The function one level up is the caller — this is your candidate for `g_rf_submit_fn`.

4. Disassemble that caller. It should:
   - Take a vertex buffer pointer (pointer to array of `rf_rhw_vertex_t`)
   - Take a vertex count
   - Loop or directly call `DrawIndexedPrimitive` / `DrawPrimitive`

5. Note the **entry point address** of that function. That goes into `g_rf_submit_fn` in `game.hpp`.

6. To confirm it's the right spot, set a breakpoint at the function entry, log the first `float` of the first vertex:
   - If it's a large value (e.g., `640.5`, `512.3`) — you're looking at already screen-space data (too late)
   - If it's a small value (e.g., `12.3`, `-5.7`, `0.001`) — world-space, this is the right spot ✅

---

### Step 4 — Verify Camera Struct Layout

Once you have `g_rf_camera`, confirm the layout:

1. In **Memory** tab, go to the camera struct address.
2. You should see 16 floats (the view matrix) starting at offset `+0x00`.
3. Immediately after (offset `+0x40`) should be 16 more floats (the projection matrix).
4. The projection matrix `[0][0]` and `[1][1]` should be around `1.0`–`2.5` (focal length).
5. The projection matrix `[3][3]` should be `0.0` (perspective, not orthographic).

If the layout differs, adjust the `rf_camera_t` struct offsets in `src/comp/game/structs.hpp`.

---

### Step 5 — Fill in the Addresses

Once confirmed, update `src/comp/game/game.hpp`:

```cpp
// Replace the TODO placeholders with your found addresses:
rf_camera_t*  g_rf_camera     = reinterpret_cast<rf_camera_t*>(0x00xxxxxx);
uintptr_t     g_rf_submit_fn  = 0x00yyyyyy;
```

Then rebuild and test — the mod will hook the submit function and resubmit draw calls with world-space geometry and real matrices to RTX Remix.

<br>

## Credits
- **xoxor4d** — Original [remix-comp-base](https://github.com/xoxor4d/remix-comp-base) framework
- [NVIDIA — RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [minhook](https://github.com/TsudaKageyu/minhook)
- [Ultimate-ASI-Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- [dxwrapper](https://github.com/elishacloud/dxwrapper)
