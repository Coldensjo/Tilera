# AI agent instructions — building Tilera

The canonical build is the pair of standalone Visual Studio solutions under `vcproj/` (Windows, MSVC v143): `vcproj/Editor/Editor.sln` and `vcproj/MapServer/MapServer.sln`. A community/experimental Linux build also exists via `CMakeLists.txt` at the repo root (system packages instead of vcpkg; see `README.md` → Build → Linux). The Linux CMake build is not the primary target — treat those two solutions as authoritative for Editor version bumps, project-file changes, and Windows-specific behavior. When editing `source/`, keep changes portable (the codebase is a Remere's Map Editor fork and is largely cross-platform already); prefer `wxCHECK_VERSION(...)` guards over unconditionally using APIs only present in newer wxWidgets, since the Linux build currently targets wxWidgets 3.2.

## Become a specialized agent

The `agents/` folder contains specialized agent personas, each an expert for a specific kind of task. **Before starting any non-trivial task, check the table below. If the task matches a persona's purpose, immediately `Read` that persona file (`agents/<Name>.md`) and adopt it** — follow its process, rules, mantra, and `BEFORE WRITING ANY CODE` checklist as if you were that agent. Announce it briefly (e.g. *"Adopting **Cleaver** for this refactor."*) and stay in character until the task is done.

| If the task is about… | Become | File |
|-----------------------|--------|------|
| Making the editor faster, cache/DOD layout, draw-call/allocation reduction | **Quicksilver** ⚡ | `agents/Quicksilver.md` |
| Profiling/measuring and fixing CPU / GPU / sync bottlenecks | **Pulse** 📊 | `agents/Pulse.md` |
| The OpenGL rendering pipeline (immediate-mode freeglut, textures, batching) | **Phosphor** 🖥️ | `agents/Phosphor.md` |
| Bugs, crashes, undefined behavior, race conditions, exception safety | **Wraith** 🐛 | `agents/Wraith.md` |
| Tile engine, brushes, map data, undo/redo, serialization, spatial indexing | **Cartographer** 🗺️ | `agents/Cartographer.md` |
| Refactoring, separation of concerns, reducing coupling, splitting god classes | **Cleaver** 🔧 | `agents/Cleaver.md` |
| Code smells, dead code, duplication, legacy-pattern cleanup | **Bloodhound** 👃 | `agents/Bloodhound.md` |
| Modernizing code to C++17 idioms | **Vanguard** 🔄 | `agents/Vanguard.md` |
| wxWidgets correctness, High-DPI, theming, `Bind()`, layout/sizers | **Loom** 🔧 | `agents/Loom.md` |
| UX polish, tooltips, accessibility, keyboard shortcuts, user feedback | **Finesse** 🎨 | `agents/Finesse.md` |
| Adding/standardizing UI icons (via `wxArtProvider`) | **Sigil** 🎨 | `agents/Sigil.md` |
| Adding Doxygen `/* */` documentation to public APIs | **Lorekeeper** 📜 | `agents/Lorekeeper.md` |

Rules for personas:

- **Match by intent, not keywords.** Pick the single closest persona; if a task spans two (e.g. a refactor that also modernizes), lead with the primary one and borrow the other's checklist.
- **The persona never overrides this file.** Build commands, the **Release | x64** verification step, and the `source/definitions.h` version-bump policy below always apply.
- **No persona fits?** Proceed as the default agent — don't force one.
- **Don't adopt a persona** for trivial one-liners, pure questions, or build/config-only chores.

## Quick reference

| Item | Value |
|------|-------|
| Solutions | `vcproj/Editor/Editor.sln` and `vcproj/MapServer/MapServer.sln` — **independent**, one project each |
| Projects | `Editor` (GUI), `MapServer` (console) |
| Platform | **x64 only** (Win32 solution configs map to x64) |
| Preferred config | **Release \| x64** |
| Toolset | MSVC **v143** (Visual Studio 2022) |
| C++ standard | C++17 |
| Dependency manager | **vcpkg** manifest mode (`vcpkg.json` at repo root) |
| Triplet | `x64-windows-static-v143` (overlay in `triplets/`) |
| Linkage | **Fully static** — static CRT (`/MT`) and static deps; no DLLs shipped |
| Outputs | `Editor_x64.exe`, `MapServer_x64.exe` in **repo root** |
| Intermediate files | `vcproj/Editor/x64/{Debug\|Release}/Editor/` and `vcproj/MapServer/x64/{Debug\|Release}/MapServer/` |
| Shared between them | only `source/`, `vcpkg.json`, and `triplets/` — no shared build files |

## Prerequisites

1. **Windows 10+**
2. **Visual Studio 2022** (or newer) with:
   - Desktop development with C++
   - MSVC v143 toolset
   - Windows 10/11 SDK
3. **[vcpkg](https://vcpkg.io/)** installed and integrated with Visual Studio (manifest mode).

Set `VCPKG_ROOT` to your vcpkg clone if it is not already set, for example:

```powershell
$env:VCPKG_ROOT = "E:\vcpkg"   # adjust to your path
```

The `.vcxproj` files enable manifest mode (`VcpkgEnableManifest=true`). Dependencies from `vcpkg.json` are installed automatically on the first build. No separate `vcpkg install` step is required, but you may pre-install manually:

```powershell
cd C:\path\to\Tilera
& "$env:VCPKG_ROOT\vcpkg.exe" install --triplet x64-windows
```

### vcpkg dependencies

Declared in `vcpkg.json`: wxwidgets, freeglut, asio, nlohmann-json, fmt, libarchive, boost-spirit, boost-asio, tomlplusplus.

## Build — Visual Studio IDE

Each program has its own solution — open whichever one you need:

1. Open `vcproj/Editor/Editor.sln` (map editor) **or** `vcproj/MapServer/MapServer.sln` (live server).
2. Set configuration to **Release** and platform to **x64**.
3. Build Solution (`Ctrl+Shift+B`).
4. Confirm the output exists in the repo root — `Editor_x64.exe` or `MapServer_x64.exe`.

Building one does **not** build the other; they no longer share a solution. Open both
solutions (or run both command lines below) when you need both executables.

## Build — command line (preferred for agents)

`OutDir` is anchored to `$(MSBuildThisFileDirectory)..\..\`, so outputs always land in the **repo root** — never under `vcproj\` — no matter how the build is invoked (solution, single `.vcxproj`, or IDE).

From the **repo root**, find MSBuild with `vswhere`, then build whichever solution you need:

```powershell
cd C:\path\to\Tilera   # repo root — required

$msbuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
  -latest -requires Microsoft.Component.MSBuild `
  -find "MSBuild\**\Bin\MSBuild.exe" | Select-Object -First 1

# map editor
& $msbuild "vcproj\Editor\Editor.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal

# live server
& $msbuild "vcproj\MapServer\MapServer.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
```

The two builds are fully independent — separate intermediate directories, separate
PCHs — so they can also be run concurrently.

A successful build ends with lines like (paths must be under the **repo root**, not `vcproj\`):

```text
Editor.vcxproj -> C:\path\to\Tilera\Editor_x64.exe
MapServer.vcxproj -> C:\path\to\Tilera\MapServer_x64.exe
```

The executables are self-contained: they import only Windows system DLLs, so **no
`.dll` files (and no VC++ redistributable) need to sit next to them**. `data/` is
still required at runtime. If a build ever deposits third-party DLLs in the repo
root again, the static triplet is not being applied — see **Static linking** below.

### Editor images

Every editor image is compiled into the executable as a byte array in
`source/pngfiles.cpp`; **nothing is loaded from `icons/` or `brushes/` at
runtime**, so neither folder ships with the binary. Those folders hold the
*source* art — editing a `.png` there changes nothing until its array is
regenerated:

```powershell
.\tools\sync_all_png_arrays.ps1                     # re-embed everything drifted (the usual one)
.\tools\sync_all_png_arrays.ps1 -DryRun             # ...or just show what would change

.\tools\sync_png_arrays.ps1                         # report drift, write nothing
.\tools\sync_png_arrays.ps1 brushes\door_locked.png # sync a single file
.\tools\sync_png_arrays.ps1 -All                    # same as sync_all_png_arrays.ps1
```

`sync_all_png_arrays.ps1` is the pre-build step; `sync_png_arrays.ps1` reports by
default and never writes unless given a file or `-All`. Pass a full relative path
when a file name exists in both folders (`brushes\pvp_zone.png` and
`icons\pvp_zone.png` are different artwork).

Then rebuild. Use `PNG_BITMAP(name)` (from `pngfiles.h`) to turn an array into a
`wxBitmap`. Images under `icons/` are embedded with an `icon_` prefix
(`icons/terrain.png` → `icon_terrain_png`) to avoid clashing with the
same-named-but-different art in `brushes/`.

### Static linking

Dependencies are built and linked statically. The moving parts, if you need to
change them:

- `triplets/x64-windows-static-v143.cmake` — overlay triplet; static CRT + static
  libs, and pins `VCPKG_PLATFORM_TOOLSET` to **v143** so vcpkg's dependencies use
  the same toolset the `.vcxproj` files declare. Without the pin, vcpkg silently
  uses the newest installed MSVC, and wxWidgets picks up STL helpers missing from
  v143's `libcpmt.lib` (`LNK2001: __std_find_last_not_of_trivial_pos_2`).
- `vcpkg.json` — registers `./triplets` via `vcpkg-configuration.overlay-triplets`.
- Both `.vcxproj` — `<VcpkgTriplet>`, `<RuntimeLibrary>MultiThreaded[Debug]</…>`,
  and the `FREEGLUT_STATIC` / `LIBARCHIVE_STATIC` defines. `WXUSINGDLL` must
  **not** be defined. Static freeglut/libarchive/openssl also need `opengl32`,
  `glu32`, `winmm`, `crypt32`, `rpcrt4` and `xmllite` on the link line.

Changing the triplet or toolset invalidates the vcpkg ABI and rebuilds all ~89
packages from source (slow — tens of minutes).

### Debug build

```powershell
& $msbuild "vcproj\Editor\Editor.sln" /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
& $msbuild "vcproj\MapServer\MapServer.sln" /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```

Debug links against `freeglutd.lib` and uses vcpkg Debug libraries.

### Clean rebuild

```powershell
& $msbuild "vcproj\Editor\Editor.sln" /t:Clean /p:Configuration=Release /p:Platform=x64
& $msbuild "vcproj\Editor\Editor.sln" /p:Configuration=Release /p:Platform=x64 /m /v:minimal
```

Cleaning one solution does not touch the other's intermediate files.

## Source layout (for compile context)

- `source/` — all C++ sources and headers, shared by both programs
- `vcproj/Editor/` — `Editor.sln`, `Editor.vcxproj` (+ `.filters`), `Editor.rc`, `editor_icon.ico`
- `vcproj/MapServer/` — `MapServer.sln`, `MapServer.vcxproj` (+ `.filters`)
- `data/` — editor metadata (tilesets, brushes, etc.); must stay next to the executables at runtime
- `vcpkg.json` — dependency manifest (repo root), shared by both projects

The two products have **no build files in common** — separate solutions, project files,
and intermediate directories. Both `.vcxproj` reference `source/` via `..\..\source\`
relative paths and compile nearly the same file list. `MapServer` is a console app
(`SubSystem=Console`) that additionally compiles `live_server_main.cpp` and defines
`__LIVE_SERVER__`; `Editor` is a Windows GUI app (`SubSystem=Windows`) that additionally
compiles `Editor.rc`.

**When you add, move, or remove a file under `source/`, update BOTH `.vcxproj` files**
(and their `.filters`) unless the file is genuinely specific to one program.

Precompiled header: `main.h` (included as `PrecompiledHeaderFile` in both projects).

## Editor version

The editor version lives in `source/definitions.h`:

```cpp
#define __RME_VERSION_MAJOR__ 2
#define __RME_VERSION_MINOR__ 0
#define __RME_SUBVERSION__ 0
```

**Whenever you change Editor behavior** (C++ under `source/` that affects the GUI app, or Editor-only project files), **increment the version** in the same change:

| Change kind | Bump |
|-------------|------|
| Bug fix, small tweak, or incremental feature | `__RME_SUBVERSION__` (+1) |
| Notable new feature or user-visible milestone | `__RME_VERSION_MINOR__` (+1), reset `__RME_SUBVERSION__` to `0` |
| Breaking change or major rewrite | `__RME_VERSION_MAJOR__` (+1), reset minor and subversion to `0` |

Do **not** bump the editor version for MapServer-only changes, `data/` metadata edits, docs, or build/config-only work unless the user asks.

`__LIVE_NET_VERSION__` in the same file is separate — bump it only when the live collaboration packet format changes incompatibly (see comment in `definitions.h`).

The version appears in the welcome dialog, About window, status bar, exported map headers, and live-client handshake (`__RME_VERSION_ID__` is derived automatically from the three version macros).

## Verify after code changes

When you modify C++ code, **run a build** before considering the task done. Check:

1. Exit code `0` from MSBuild.
2. Expected `.exe` updated in repo root.
3. No new compiler errors in the build log.
4. If Editor behavior changed, `source/definitions.h` version was incremented (see **Editor version** above).

For verbose errors:

```powershell
& $msbuild "vcproj\Editor.sln" /p:Configuration=Release /p:Platform=x64 /m /v:normal
```

Build logs are also written under `vcproj/Editor/x64/{Configuration}/Editor/` and
`vcproj/MapServer/x64/{Configuration}/MapServer/`.

## Troubleshooting

| Problem | What to try |
|---------|-------------|
| `MSBuild` not found | Install VS 2022 C++ workload, or use `vswhere` path above |
| vcpkg / missing headers (`wx/`, `boost/`, etc.) | Set `VCPKG_ROOT`; rebuild so manifest install runs; or run `vcpkg install --triplet x64-windows` from repo root |
| Wrong platform | Use **x64**, not Win32 |
| LTCG / incremental link warnings | Usually non-fatal; a full rebuild often clears them |
| `error C3859: PCH` / PCH issues | Clean then rebuild; ensure `main.h` is unchanged in incompatible ways across TUs |
| Link errors for `archive`, `freeglut`, wx libs | vcpkg triplet must be `x64-windows`; Debug needs Debug vcpkg libs |
| `.exe` lands in `vcproj\` instead of repo root | Should not happen — `OutDir` is anchored to `$(MSBuildThisFileDirectory)..\..\` (repo root). If it does, the `.vcxproj` `OutDir` was reverted to `$(SolutionDir)..\`; restore the `MSBuildThisFileDirectory` form |

Hard-coded fallback library paths in `.vcxproj` (e.g. `C:\vcpkg\...`) are developer-specific. The build should work through **vcpkg manifest integration** when `VCPKG_ROOT` and VS vcpkg integration are configured correctly.

## Do not

- Attempt to build on macOS (unsupported; only Windows and Linux have build systems).
- Add further build systems beyond `vcproj/` (Windows) and `CMakeLists.txt` (Linux) unless explicitly requested.
- Commit build artifacts: `*.exe`, `*.pdb`, `*.obj`, `vcproj/*/x64/`, `.vs/`, `build/`, `Editor`, `MapServer`.
- Expect `data/` alone to provide Tibia client sprites; runtime needs separate client asset paths (see `README.md`).

## More documentation

- `README.md` — features, MapServer usage, client assets, settings
- `mapserver.cfg.example` — MapServer configuration template