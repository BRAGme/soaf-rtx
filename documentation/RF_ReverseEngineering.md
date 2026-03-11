# Red Faction RTX Remix — Reverse Engineering Guide

**Target executable:** `RF_120na.exe`  
**Base address:** `0x00400000` (standard Win32 non-ASLR)  
**D3D version:** Direct3D 8 (via `d3d8.dll`, converted to D3D9 by dxwrapper)

---

## Overview

Red Faction submits geometry using **pre-transformed vertices** (`D3DFVF_XYZRHW`), meaning vertices are already in screen space when they reach the D3D API. RTX Remix cannot ray trace screen-space geometry because all 3D world information has been discarded.

To enable RTX Remix ray tracing, this mod needs:

1. **The camera struct address** — contains view and projection matrices
2. **The geometry submit function address** — the function that transforms and submits geometry (to hook and intercept before pre-transformation)

Once both are found via x32dbg and filled in to `src/comp/game/game.cpp`, the mod can reconstruct world-space draw calls that RTX Remix can ray trace.

---

## Key D3D8 Vtable Offsets

| Function | Vtable Offset | Notes |
|---|---|---|
| `SetTransform` | `0x5C` | Used to set view/projection matrices |
| `DrawPrimitive` | `0x64` | Non-indexed draw call |
| `DrawIndexedPrimitive` | `0x68` | Indexed draw call (most geometry) |

These offsets are fixed for the D3D8 `IDirect3DDevice8` interface and can be used to set hardware breakpoints or search for vtable call patterns in the disassembly.

---

## Finding the Camera Struct Address

### Method A — Break on SetTransform(D3DTS_VIEW)

This is the most reliable method. RF calls `SetTransform` to upload the view matrix to D3D before rendering each frame.

**Steps in x32dbg:**

1. Open the **Symbols** tab
2. Click `d3d8.dll` (or `dxwrapper.dll`) in the left panel
3. In the command bar at the bottom, type:
   ```
   bp d3d8.SetTransform
   ```
   Or search for `SetTransform` in the symbols list and press **F2** to set a breakpoint.

4. **Run the game** — when the breakpoint hits:
   - Check the **Stack** panel for the arguments
   - Argument 1 (`[esp+4]`) is the `D3DTRANSFORMSTATETYPE` — look for value `2` (`D3DTS_VIEW`)
   - Argument 2 (`[esp+8]`) is a pointer to the matrix — **that pointer IS the view matrix in memory**

5. When you hit the breakpoint with `arg1 == 2`:
   - Note the matrix pointer value in `arg2`
   - Open **Call Stack** tab and click the RF_120na.exe frame just above the d3d8 call
   - In the disassembly at that frame, look for the instruction that loaded the matrix pointer:
     ```asm
     mov  eax, [0x00XXXXXX]   ; load pointer to camera struct
     push eax                  ; push as SetTransform arg
     ```
   - The address `0x00XXXXXX` in that `mov` instruction is your camera struct pointer location

6. To verify: continue running, rotate the camera in-game, break again and check that the matrix values have changed.

---

### Method B — Memory scan for matrix values

1. In x32dbg, go to **Memory Map** tab
2. Right-click → **Find Pattern** (or use **Ctrl+B**)
3. Search for a known perspective matrix pattern in RF memory:
   - If you know the FOV, the top-left value of the projection matrix is approximately `1/tan(FOV/2)`
   - For 75° FOV: search for float `1.303` (hex: `0x3FA6B852`) or nearby values
4. Cross-reference the found addresses with what SetTransform receives as arg2

---

### Method C — String search

1. Go to **References** tab → search for `"camera"` or `"view"` or `"render"` in the RF_120na.exe module region
2. Functions that reference camera-related strings often read from the camera struct
3. Follow cross-references to find where the struct pointer is stored globally

---

## What the Camera Struct Lookup Typically Looks Like

Once you've walked up the call stack from SetTransform, you should see something like:

```asm
; RF SetViewMatrix or similar function
push    ebp
mov     ebp, esp
mov     eax, [0x00A1B2C3]    ; <-- global var: pointer to rf_camera_t
test    eax, eax
jz      short 0x00XXXXXX
lea     ecx, [eax + 0]       ; offset to view matrix within struct (may be 0)
push    ecx
push    2                     ; D3DTS_VIEW
push    [ebp + 8]             ; D3D8 device pointer
call    dword ptr [ecx + 5Ch] ; SetTransform via vtable offset 0x5C
```

In this example:
- `0x00A1B2C3` is the address of the global pointer variable
- `*(0x00A1B2C3)` is the actual `rf_camera_t*`
- Fill into `game.cpp`: `PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(g_rf_camera, rf_camera_t*, "A1 C3 B2 A1 00", 1, 0x00A1B2C3);`

---

## Finding the Geometry Submit Function

RF transforms vertices on the CPU before calling `DrawIndexedPrimitive`. To intercept world-space geometry, we need to hook the function that performs this transformation.

### Step 1 — Find all DrawIndexedPrimitive call sites in RF_120na.exe

In x32dbg **CPU** tab, right-click in the disassembly → **Search for** → **Current Module** → **Command**

Search for:
```
call dword ptr [ecx+68h]
```
or
```
call dword ptr [edx+68h]
```
or (using hex pattern search):
```
FF 51 68
```
```
FF 52 68
```

These are vtable calls to D3D8's `DrawIndexedPrimitive` (offset `0x68`).

### Step 2 — Identify the geometry submit function

For each call site found:
1. Scroll up from the `call [ecx+68h]` instruction to the start of the enclosing function
2. Look for a function that:
   - Receives a vertex buffer pointer and vertex count as arguments
   - Contains a loop that transforms vertices (multiply by matrix, perspective divide)
   - Ends with a call to `DrawIndexedPrimitive` or `DrawPrimitive`

### Step 3 — Verify with a breakpoint

Set a breakpoint at the start of the candidate function and run the game. When it hits:
- Step through slowly in the disassembly
- You should see vertex transformation code (matrix multiply, divide by W)
- The input vertices should have a `z` value that makes sense for 3D world space

### Step 4 — Record addresses

Once confirmed:
- `hk_addr__rf_submit_geometry` = address of the first instruction of this function
- `retn_addr__rf_submit_geometry` = `hk_addr + 5` (after the 5-byte hook jump is placed)

---

## Filling in the Addresses

Once you have found the addresses, update `src/comp/game/game.cpp`:

### For the camera struct pointer:

```cpp
// If x32dbg showed "mov eax, [0x00A1B2C3]" loading the camera pointer:
PATTERN_OFFSET_DWORD_PTR_CAST_TYPE(g_rf_camera, rf_camera_t*, "A1 C3 B2 A1 00", 1, 0x00A1B2C3);
```

- The pattern bytes are the raw bytes of the `mov eax, [addr]` instruction (little-endian address)
- `byte_offset = 1` skips the `A1` opcode to reach the 4-byte address
- The static address (`0x00A1B2C3`) is used when the game is launched with `-no_pattern`

### For the geometry submit hook:

```cpp
PATTERN_OFFSET_SIMPLE(hk_addr__rf_submit_geometry,   "XX XX XX XX XX", 0, 0x00YYYYYY);
PATTERN_OFFSET_SIMPLE(retn_addr__rf_submit_geometry, "XX XX XX XX XX", 0, 0x00YYYYYY + 5);
```

Replace `XX XX XX XX XX` with the byte pattern of the first 5 bytes of the function, and `0x00YYYYYY` with the static address.

---

## Verifying the Camera Is Captured Correctly

After filling in `g_rf_camera`:

1. Start the game and open the mod's ImGui menu (**F4**)
2. Enable the **RHW Bypass** toggle
3. The **Statistics** section should show `RHW Draw Calls Detected` counting up
4. If `g_rf_camera` is valid, those draw calls will be reconstructed in world space
5. In RTX Remix developer mode, you should see geometry appearing in the ray-traced scene

If geometry appears at incorrect positions:
- The struct layout may be wrong (view and proj matrices may be swapped, or there may be padding before them)
- Check the actual matrix values at the captured address in x32dbg memory view
- Adjust the `rf_camera_t` struct in `structs.hpp` as needed

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `g_rf_camera` stays null | Wrong address or pattern | Re-check with SetTransform breakpoint |
| Geometry in wrong position | Struct layout mismatch | Check padding/offsets in `rf_camera_t` |
| RHW count = 0 | Game not using D3DFVF_XYZRHW | Verify with `lookat_vertex_decl` in renderer |
| Crash after hook | Wrong hook size (< 5 bytes overwritten) | Check instruction size at hook address |
| Game doesn't render | Camera matrices invalid | Temporarily set `g_rf_camera = nullptr` to bypass |
