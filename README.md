<h1 align="center">SOAF RTX Remix Compatibility Mod</h1>

<br>

<div align="center" markdown="1"> 

A compatibility mod for **The Sum of All Fears** (SOAF.exe, RSE engine, 2002) to work with NVIDIA's [RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix).

<br>

</div>


## Overview
This mod provides compatibility fixes and debugging tools for running The Sum of All Fears with RTX Remix. It addresses RSE engine-specific rendering issues and provides tools to diagnose texture hash problems.

### Features

#### 1. Backface Culling Fix
The RSE engine uses the fixed-function pipeline and sets `D3DRS_CULLMODE` to `D3DCULL_CW` or `D3DCULL_CCW`, causing RTX Remix to miss the back sides of geometry. This is critical for proper reflections and indirect lighting.

**The mod fixes this by:**
- Detecting 3D perspective draw calls (vs. orthographic HUD/UI)
- Forcing `D3DCULL_NONE` on 3D geometry
- Preserving HUD rendering (skips orthographic projections where `m[3][3] == 1.0f`)
- Togglable via ImGui menu (enabled by default)

#### 2. Texture Hash Tracker
RTX Remix sometimes assigns incorrect material tags due to texture pointer reuse and hash collisions. This tracker helps diagnose the issue.

**Features:**
- Tracks all texture creation events
- Logs **pointer reuse events** to console (prime suspects for wrong material assignments)
- Shows live count of tracked textures in ImGui
- Can dump full texture table to console for analysis
- Togglable via ImGui menu (enabled by default)

#### 3. Known Limitations

#### CPU-Side Frustum Culling
The RSE engine performs CPU-side frustum culling before submitting geometry to D3D. This causes some meshes to be culled incorrectly, especially at screen edges. **This is not fixed by this mod** and requires finding and patching the frustum cull routine in SOAF.exe via reverse engineering tools (x64dbg, Cheat Engine, etc.).

Placeholder code for this fix is included in `src/comp/game/game.hpp` and `game.cpp` for when the address is found.

<br>

### Loading Chain

The mod loads as part of the following chain:
```
SOAF.exe
  └─ dxwrapper.dll       (D3D8→D3D9 conversion + loads d3d9.dll)
       └─ d3d9.dll        (RTX Remix bridge client)
            └─ dinput8.dll (Ultimate ASI Loader)
                 └─ plugins/soaf-rtx.asi  ← THIS MOD
```

<br>

### Building

#### Prerequisites
- Visual Studio 2022
- premake5 (included in `tools/` directory)

#### Steps
1. Clone the repository:
   ```bash
   git clone --recurse-submodules https://github.com/BRAGme/soaf-rtx.git
   ```

2. (Optional) Set environment variables:
   - `SOAF_GAME_DIR` - Path to your SOAF installation folder
   - `SOAF_GAME_EXE` - Name of the game executable (e.g., `SOAF.exe`)

3. Generate Visual Studio project files:
   ```bash
   generate-buildfiles_vs22.bat
   ```

4. Open `build/soaf-rtx.sln` in Visual Studio 2022 and build.

5. The compiled `.asi` file will be in `build/bin/Release/plugins/soaf-rtx.asi` (or in your `SOAF_GAME_DIR/plugins/` if you set the environment variable).

<br>

### Installation

1. **Install dxwrapper** (for D3D8→D3D9 conversion):
   - Download from [dxwrapper releases](https://github.com/elishacloud/dxwrapper/releases)
   - Copy `dxwrapper.dll` and `dxwrapper.ini` to your SOAF directory
   - Configure `dxwrapper.ini` for D3D8→D3D9 mode

2. **Install RTX Remix**:
   - Copy `d3d9.dll` (RTX Remix bridge) to your SOAF directory

3. **Install Ultimate ASI Loader**:
   - Download from [Ultimate ASI Loader releases](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases)
   - Rename it to `dinput8.dll` and copy to your SOAF directory

4. **Install this mod**:
   - Create a `plugins` folder in your SOAF directory
   - Copy `soaf-rtx.asi` to `plugins/soaf-rtx.asi`

5. **Launch the game** - The mod will automatically load and display a console window with startup info.

<br>

### Usage

#### ImGui Menu
Press **F4** to open the debug menu. You'll find:
- **SOAF Culling Fix** - Toggle backface culling override
- **SOAF Texture Tracker** - View tracked textures and dump to console

#### Console Output
The mod logs important events to the console window:
- Texture pointer reuse events (suspects for material tag issues)
- Window class detection
- Initialization status

<br>

### Texture Hash Root Cause Analysis

RTX Remix uses texture hashes to assign material tags. Wrong tags occur due to:
1. **Pointer reuse** - D3D9 reuses texture pointers after Release(), confusing Remix's internal tracking
2. **Identical pixel content** - Different textures with the same pixels get the same hash
3. **Remix hashing bugs** - Edge cases in Remix's hash calculation

The texture tracker helps identify pointer reuse events, which are the most common cause.

<br>

### Credits
- **xoxor4d** - Original [remix-comp-base](https://github.com/xoxor4d/remix-comp-base) framework
- [NVIDIA - RTX Remix](https://github.com/NVIDIAGameWorks/rtx-remix)
- [Dear ImGui](https://github.com/ocornut/imgui)
- [minhook](https://github.com/TsudaKageyu/minhook)
- [Ultimate-ASI-Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
- [dxwrapper](https://github.com/elishacloud/dxwrapper)
