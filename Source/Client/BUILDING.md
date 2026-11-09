# Building the Client on Windows (CLion + MinGW + vcpkg)

This guide takes a blank Windows machine to a running debug build of the client in CLion.

## How the build fits together

| Piece | What it does |
|-------|--------------|
| **CMake** (4.1+) | Build system. CLion ships its own (`CLion/bin/cmake`). |
| **MinGW-w64 GCC** | Compiler. CLion ships one (`CLion/bin/mingw`). MSVC is **not** supported: the build uses GCC-only flags like `-mwindows`. |
| **vcpkg** | Package manager. Downloads and builds SDL2, SDL2_image/mixer/net, libpng, zlib, freetype, imgui and glad, as listed in `vcpkg.json`. |
| **vcpkg triplet** | `x64-mingw-dynamic`. `CMakeLists.txt` picks this automatically on Windows, so the libraries are built with MinGW and match the client. |

The first configure is slow (10–20 minutes) because vcpkg builds every dependency from source. Later configures reuse vcpkg's binary cache and are fast.

---

## 1. Install the tools

1. **Git for Windows**: https://git-scm.com/download/win
2. **CLion**. Its bundled MinGW and CMake are enough, so you don't need MSYS2 or Visual Studio.

## 2. Install vcpkg

Use a short path with no spaces. In PowerShell:

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
```

> Clone the full history (don't use `--depth 1`). `vcpkg.json` pins package versions with a
> `builtin-baseline` commit, and vcpkg has to be able to find that commit in its git history.

## 3. Set environment variables

vcpkg needs two things from your environment:

- `VCPKG_ROOT` tells `CMakeLists.txt` where to find the vcpkg toolchain.
- The **MinGW `bin` directory must be on `PATH`**. vcpkg builds each library in its own CMake process,
  and that process finds the compiler by searching `PATH` for `x86_64-w64-mingw32-gcc.exe`.

In PowerShell (adjust the CLion path if yours is different):

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")

$mingw = "$env:LOCALAPPDATA\Programs\CLion\bin\mingw\bin"
$path  = [Environment]::GetEnvironmentVariable("Path", "User")
[Environment]::SetEnvironmentVariable("Path", "$mingw;$path", "User")
```

**Restart CLion completely** after this. It only reads environment variables when it starts.

Check in a new terminal:

```powershell
echo $env:VCPKG_ROOT                 # C:\vcpkg
x86_64-w64-mingw32-gcc --version     # gcc ... 15.x
```

> **Toolbox users:** if CLion was installed with JetBrains Toolbox, the bundled MinGW is under
> `%LOCALAPPDATA%\Programs\CLion\bin\mingw\bin` or
> `%LOCALAPPDATA%\JetBrains\Toolbox\apps\CLion\...\bin\mingw\bin`. To find yours, open
> *Settings → Build, Execution, Deployment → Toolchains* and look at the C Compiler path.

## 4. Configure CLion

1. **File → Open** `Source/Client` (the folder that contains `CMakeLists.txt`), not the repo root.
2. *Settings → Build, Execution, Deployment → Toolchains*: the default **MinGW** (bundled) toolchain is fine.
3. *Settings → Build, Execution, Deployment → CMake*: the default **Debug** profile is fine. You don't need extra CMake options.
   - If you'd rather not set `VCPKG_ROOT` globally, put this in *CMake options* instead:
     `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake`
4. If you configured before setting up vcpkg, clear the cache: **Tools → CMake → Reset Cache and Reload Project**.
   The old cache may still contain the wrong `VCPKG_TARGET_TRIPLET`.

A successful reload log contains:

```
-- Windows host, using vcpkg triplet: x64-mingw-dynamic
-- Running vcpkg install
...
-- SDL2 libraries linked via vcpkg
-- Configuring done
```

## 5. Build and run

- Build the **TheLastGate** target (Ctrl+F9). After the build, every vcpkg DLL and the MinGW runtime DLLs
  are copied next to `TheLastGate.exe` in `cmake-build-debug/`.
- The client needs the original Astonia graphics files to run. Download them from https://www.brockhaus.org/merc2.html
  (see `README.MD`), and move the images from `gfx/overlays/` back into `gfx/`.
- Set the run configuration's **Working directory** to the folder that holds the game data.

### Other build types

| CLion profile / `-DCMAKE_BUILD_TYPE` | Notes |
|---|---|
| `Debug` | Default. |
| `Release` | `-O3`, stripped binary. |
| `-DHOME_COPY=ON` | Connects to the home server (adds `-DHOMECOPY`). |
| `Package` target | Builds the release zip. |

---

## Command-line build (no CLion)

From a terminal where MinGW is on `PATH` and `VCPKG_ROOT` is set:

```powershell
cd Source\Client
cmake -B cmake-build-debug -S . -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

CI uses MSYS2 and the `windows` preset in `CMakePresets.json` (`cmake --preset windows`). That preset
uses clang and expects `CLIENT_DIR` to be set as well. See `.github/workflows/build.yml`.

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `VCPKG_ROOT environment variable not set` followed by `Could not find a package configuration file provided by "SDL2"` | vcpkg isn't hooked up. Do steps 2 and 3, restart CLion, then reset the cache. |
| Log says `using vcpkg triplet: x64-windows` | The cache is stale from an older `CMakeLists.txt`. Reset the cache, or delete `cmake-build-debug/`. |
| vcpkg fails with `Unable to find a valid toolchain` / `CMAKE_C_COMPILER not set` / `x86_64-w64-mingw32-gcc not found` | MinGW `bin` isn't on `PATH` for vcpkg. Redo step 3 and restart CLion. |
| vcpkg error about `builtin-baseline` / unknown commit | vcpkg was cloned shallow or is very old. Run `git -C C:\vcpkg fetch --unshallow` (or `git pull`), then `bootstrap-vcpkg.bat`. |
| Link errors such as `undefined reference to __imp_SDL_...` or `.lib` files | The libraries were built for MSVC (`x64-windows`). Delete `cmake-build-debug/` and reconfigure. |
| Executable starts and exits right away / "DLL not found" | Run from `cmake-build-debug/`, where the post-build step copied the DLLs, or check that the run configuration's working directory has the game data. |
| Path-length errors during the vcpkg build | Keep vcpkg at a short path like `C:\vcpkg`, and/or enable Windows long paths. |
