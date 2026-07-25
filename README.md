# Tilera

Remere's Map Editor fork with a standalone **MapServer** for live collaborative map editing.

## Build

### Windows

**Requirements**

- Visual Studio 2022 (MSVC v143)
- [vcpkg](https://vcpkg.io/)

**Steps**

The two programs have independent solutions and can be built separately:

| Program | Solution | Output |
|---------|----------|--------|
| Map editor (GUI) | `vcproj/Editor/Editor.sln` | `Editor_x64.exe` |
| Live server (console) | `vcproj/MapServer/MapServer.sln` | `MapServer_x64.exe` |

1. Clone this repo.
2. Open `vcproj/Editor/Editor.sln` (or `vcproj/MapServer/MapServer.sln`) in Visual Studio.
3. Select **Release | x64**.
4. Build the solution. The `.exe` lands in the repo root.

Build both by opening each solution in turn (they share `source/` but nothing else).

### Linux

Built via CMake (`CMakeLists.txt` at repo root) against system packages instead of vcpkg.

**Requirements** (Ubuntu/Debian package names)

```
sudo apt-get install -y build-essential cmake libwxgtk3.2-dev libwxgtk-media3.2-dev \
  freeglut3-dev libgl-dev libboost-dev libboost-thread-dev nlohmann-json3-dev \
  libasio-dev libtomlplusplus-dev libarchive-dev libfmt-dev
```

**Steps**

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Outputs `Editor` and `MapServer` in the repo root, alongside `data/`.

### Start the server

copy `mapserver.cfg.example` to `mapserver.cfg` next to `MapServer_x64.exe`, edit it, then run:

```text
MapServer_x64.exe
```

## Connect from the editor

1. Run `Editor_x64.exe`.
2. Go to **Network → Connect to Server...**
3. Enter host, port, display name, and password (if the server uses one).
4. Click **Connect**.

You edit the hosted map in real time with other connected editors.

On first connect, the editor downloads the server's client assets (this can take a minute for large `.spr` files). See [Client assets](#client-assets) for paths and security notes.

## Client assets

The editor and MapServer need Tibia client files to display items and sprites. MapServer also **sends these files to connecting editors** during live join so everyone uses the same client version as the hosted map.

### MapServer

Point MapServer at separate folders (see `mapserver.cfg`):

- **ASSETS** — `Tibia.dat`, `Tibia.spr`, `Tibia.otfi`
- **ITEMS** — `items.otb`, `items.xml`
- **MONSTERS** / **NPCS** — creature XML (used for spawn looktypes)

### Asset sharing warning

**Anyone who can connect to your live server receives a copy of the client assets you host** (including `Tibia.spr`), saved locally in the cache folder above. Treat live access like sharing your sprite archive:

- Use a **strong session password** and only give access to people you trust.
- Assume connected mappers **can keep and reuse** those files outside the editor.
- The cache is **plain, unencrypted** Tibia format — do not rely on hidden paths for protection.
- If your `.spr` is valuable, consider a **stripped asset pack** (only sprites/items your map needs), legal agreements with mappers, or accepting that full client art cannot be fully protected once sent to a client machine.

## License

MIT [LICENSE](LICENSE)